# VS Code / Cursor Setup (CMake + Pico SDK)

This note describes a clean, repeatable setup for C code navigation and diagnostics in this repository.
It is written for VS Code and Cursor (same extension model).

## Goal

After setup, these features should work reliably:

- Go to Definition (`F12` / Ctrl+Click)
- Hover type/info
- Find references
- Correct include and macro resolution for Pico SDK / Pico Extras

## Recommended Language Stack

Use one language engine for C/C++:

- `llvm-vs-code-extensions.vscode-clangd` (recommended)
- `ms-vscode.cmake-tools` (required for CMake workflow)

Avoid running two C/C++ engines at the same time (for example `clangd` + full IntelliSense from `cpptools`), because diagnostics and navigation can conflict.

## Workspace Settings

Use `.vscode/settings.json` with explicit source/build dirs and explicit SDK paths:

```json
{
  "cmake.sourceDirectory": "${workspaceFolder}/core",
  "cmake.buildDirectory": "${workspaceFolder}/build/vscode",
  "cmake.configureOnOpen": true,
  "cmake.configureSettings": {
    "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
    "PICO_SDK_PATH": "${workspaceFolder}/sdk/pico-sdk",
    "PICO_EXTRAS_PATH": "${workspaceFolder}/sdk/pico-extras",
    "PICO_BOARD": "waveshare_rp2350_zero"
  },
  "cmake.environment": {
    "PICO_SDK_PATH": "${workspaceFolder}/sdk/pico-sdk",
    "PICO_EXTRAS_PATH": "${workspaceFolder}/sdk/pico-extras"
  },
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build/vscode",
    "--query-driver=C:/pico/sdk/gcc-arm-none-eabi/bin/arm-none-eabi-*.exe"
  ]
}
```

Notes:

- `PICO_BOARD` should match your target board.
- `--query-driver` must match your actual GCC ARM toolchain path.
- If you do not want auto-configure on project open, set `"cmake.configureOnOpen": false`.

## Setup Procedure

1. Open the repository root folder (not a single file).
2. Install/enable `CMake Tools` and `clangd`.
3. Run `CMake: Delete Cache and Reconfigure` once.
4. Wait for configure to finish without errors.
5. Confirm `build/vscode/compile_commands.json` exists.
6. Run `clangd: Restart language server` (or reload window).

## Verification Checklist

Quick checks:

- `build/vscode/CMakeCache.txt` contains correct `PICO_SDK_PATH` and `PICO_EXTRAS_PATH`.
- `build/vscode/compile_commands.json` contains entries for `core/src/main.c`.
- In `core/src/main.c`, `#include "pico/stdlib.h"` resolves (no red underline).
- `F12` on Pico symbols opens SDK headers.

## Troubleshooting (Root-Cause Oriented)

### Error: SDK version requirement fails (for example `Require at least Raspberry Pi Pico SDK version 2.0.0`)

Meaning:

- CMake is reading a different SDK than expected (often from global environment), not the repository SDK.

Fix:

- Force `PICO_SDK_PATH` and `PICO_EXTRAS_PATH` in workspace settings (`cmake.configureSettings` and `cmake.environment`).
- Delete CMake cache and reconfigure.

### Error: `pico/stdlib.h` not found

Meaning:

- Language server does not have correct compile arguments, include paths, or driver info.

Fix:

- Ensure `compile_commands.json` exists in `build/vscode`.
- Ensure `clangd.arguments` includes the correct `--compile-commands-dir`.
- Ensure `--query-driver` matches real ARM GCC path.
- Restart clangd after configure.

### Many unrelated red diagnostics at once

Meaning:

- Usually one root failure early in configuration (wrong SDK path, missing compile database, wrong board/toolchain) causes cascaded editor errors.

Fix:

- Resolve the first configure error.
- Reconfigure and restart language server.

