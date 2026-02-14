# VS Code / Cursor Setup (CMake + Pico SDK + Heavy)

This note describes a reproducible editor setup for this repository.
It is intentionally generic and focuses on stable workflow, not incident history.

## Goal

After setup, these features should work consistently:

- Go to Definition (`F12` / Ctrl+Click)
- Hover types and symbols
- Find references
- Correct include/macro resolution for Pico SDK, Pico Extras, and Heavy-generated headers

## Tooling Model

Use one C/C++ language engine:

- `llvm-vs-code-extensions.vscode-clangd` (recommended)
- `ms-vscode.cmake-tools` (project configure support)

Avoid running two C/C++ language engines at the same time (`clangd` + full `cpptools` IntelliSense), because diagnostics/navigation may conflict.

## Repository Workflow Assumptions

- Firmware builds are run via:
  - `python scripts/build_firmware.py <patch.pd> [flags...]`
- `clangd` reads:
  - `${workspaceFolder}/build/compile_commands.json`
- Compilation database source is patch-local:
  - `build/<patch>/firmware-build/compile_commands.json`
- If your branch does not auto-export compile commands to `build/`, copy it manually after each configure/build.
- Build tools should be deterministic:
  - prefer repo `venv` and pinned Ninja path in workspace settings

## Workspace Settings (Current Project)

Keep `.vscode/settings.json` aligned with repository layout:

```json
{
  "cmake.sourceDirectory": "${workspaceFolder}/core",
  "cmake.buildDirectory": "${workspaceFolder}/build/vscode",
  "cmake.configureSettings": {
    "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
    "CMAKE_MAKE_PROGRAM": "${workspaceFolder}/venv/Scripts/ninja.exe",
    "PICO_SDK_PATH": "${workspaceFolder}/sdk/pico-sdk",
    "PICO_EXTRAS_PATH": "${workspaceFolder}/sdk/pico-extras"
  },
  "cmake.environment": {
    "PICO_SDK_PATH": "${workspaceFolder}/sdk/pico-sdk",
    "PICO_EXTRAS_PATH": "${workspaceFolder}/sdk/pico-extras"
  },
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build"
  ]
}
```

Notes:

- `CMAKE_MAKE_PROGRAM` pinning avoids ambiguous Ninja resolution in mixed Python/toolchain environments.
- `--query-driver` can be added when needed (often useful on Windows ARM GCC setups). Keep machine-specific values in user-local settings when possible.

## Optional: Local `.clangd` Overlay for Full Navigation

If some symbols are behind feature flags and do not appear in current compile commands, add a local `.clangd` file at repo root to extend editor indexing only.

Example:

```yaml
If:
  PathMatch: ^core/src/.*\.(c|h)$
CompileFlags:
  Add:
    - -DENABLE_I2C_DMA
    - -DENABLE_OLED
    - -DENABLE_USB_MIDI
    - -DENABLE_MPR121
    - -DENABLE_WS2812
```

Important:

- This affects `clangd` navigation/diagnostics only.
- It does **not** change firmware build behavior (`python scripts/build_firmware.py ...` remains the source of truth).
- In this repository `.clangd` is ignored by Git (`.gitignore`) to keep it developer-local by default.
- After changes, run `clangd: Restart language server`.

## Setup Procedure

1. Open the repository root folder (not an individual file).
2. Ensure submodules are present:
   - `git submodule update --init --recursive`
3. Create/activate project venv and install Python deps:
   - Windows PowerShell:
     - `.\venv\Scripts\Activate.ps1`
     - `python -m pip install -r requirements.txt`
   - Linux/macOS:
     - `source venv/bin/activate`
     - `python -m pip install -r requirements.txt`
4. Install/enable `clangd` and `CMake Tools`.
5. Run one firmware build through project entrypoint (example):
   - `python scripts/build_firmware.py pd-patches/hv_sine_simple_test.pd --name hv_sine_simple_test --no-clean`
6. Ensure `build/compile_commands.json` exists and is fresh.
   - If missing/outdated, copy from patch-local build:
     - Windows PowerShell:
       - `Copy-Item "build/<patch>/firmware-build/compile_commands.json" "build/compile_commands.json" -Force`
     - Linux/macOS:
       - `cp "build/<patch>/firmware-build/compile_commands.json" "build/compile_commands.json"`
7. Run `clangd: Restart language server` (or reload window).

## Verification Checklist

- `build/compile_commands.json` exists.
- `build/compile_commands.json` has entries for:
  - `core/src/main.c`
  - `core/src/patch_api.c`
- In `core/src/main.c`, `#include "pico/stdlib.h"` resolves.
- In `core/src/patch_api.c`, `#include "HvHeavy.h"` resolves.
- `F12` on Pico/Heavy symbols opens real headers.

## Troubleshooting

### Many files suddenly become red

Typical root cause:

- `clangd` is using missing or stale `compile_commands.json`.

Actions:

- Re-run `build_firmware.py` for the patch you are currently working with.
- Refresh `build/compile_commands.json` from `build/<patch>/firmware-build/compile_commands.json`.
- Restart clangd.

### `pico/*` headers are unresolved

Typical root cause:

- wrong or missing SDK include paths in compilation database/configure environment.

Actions:

- Verify `PICO_SDK_PATH` and `PICO_EXTRAS_PATH` in `.vscode/settings.json`.
- Reconfigure (`CMake: Delete Cache and Reconfigure`) and restart clangd.

### Heavy headers (for example `HvHeavy.h`) are unresolved

Typical root cause:

- compile database does not include current patch-generated `build/<patch>/c` include path.

Actions:

- Re-run `python scripts/build_firmware.py <patch.pd> --name <patch> --no-clean`.
- Refresh `build/compile_commands.json` from `build/<patch>/firmware-build/compile_commands.json`.
- Confirm fresh `compile_commands.json` and restart clangd.

### Tool selection appears unstable

Typical root cause:

- implicit `PATH` selection picks different binaries across sessions.

Actions:

- Keep using repo `venv`.
- Pin `CMAKE_MAKE_PROGRAM` to repo-local Ninja.
- Check resolved binaries:
  - Windows: `where cmake`, `where ninja`
  - Linux/macOS: `which cmake`, `which ninja`
