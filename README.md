# RP2350 Pure Data Build System

Build system for compiling Pure Data (Pd) patches into firmware for Raspberry Pi RP2350 via hvcc.

## Description

This project compiles Pure Data patches into firmware for the RP2350 microcontroller, which features FPU and DSP capabilities suitable for real-time audio processing generated from Pd patches.

## Requirements

- **Python 3.9+**
- **Python dependencies** (`pip install -r requirements.txt` installs `hvcc`, `ninja`, and `cmake`)
- **Raspberry Pi Pico SDK** (for RP2350) - included as git submodule
- **CMake 3.13+**
- **Host C/C++ compiler** (needed for Pico SDK host tools like `pioasm`; e.g. LLVM Clang or Visual Studio Build Tools)
- **ARM GCC toolchain** (arm-none-eabi-gcc)
- **picotool** (optional, for manual flashing) - included as git submodule

## Installation

After cloning the repository, initialize git submodules:

```bash
git submodule update --init --recursive
```

Install Python dependencies:

```bash
pip install -r requirements.txt
```

## Usage

Build a firmware from a Pure Data patch:

```bash
python scripts/build_firmware.py pd-patches/your_patch.pd
```

The firmware UF2 file will be generated at:
```
build/<patch_name>/firmware-build/rp2350_puredata_firmware.elf.uf2
```

### Feature Flags

Defaults (when no flags are provided):
- `ENABLE_WS2812=ON` (opt-out)
- `ENABLE_OLED=OFF`
- `ENABLE_MPR121=OFF`
- `ENABLE_USB_MIDI=OFF`
- `ENABLE_I2C_DMA=OFF`

CLI flags (single argument per feature):
```bash
# Enable optional features
python scripts/build_firmware.py pd-patches/your_patch.pd --oled --mpr121 --usb-midi --i2c-dma

# Disable WS2812 (ON by default)
python scripts/build_firmware.py pd-patches/your_patch.pd --no-ws2812
```

You can always pass explicit CMake defines:
```bash
python scripts/build_firmware.py pd-patches/your_patch.pd -D ENABLE_OLED=ON -D ENABLE_WS2812=OFF
```

### Logging Levels

The build script supports multiple verbosity levels:

- `--log-level normal` (default) - brief steps, warnings/errors visible
- `--log-level verbose` or `-v` - real-time build progress streaming
- `--log-level debug` or `-d` - full commands and paths
- `--log-level quiet` or `-q` - errors only

Build log is saved to `build/<patch>/build_firmware.log` (full subprocess output in verbose/debug modes; script messages only in normal/quiet).

### Windows USB note (CDC / COM port)

On some Windows setups, the default CDC-only build (no `--usb-midi`) may enumerate as a broken device (e.g. **Code 10**) and no COM port is created. See `docs/USB_DEBUG.md` for current observations and workarounds.

## Configuration

### I2S Pin Configuration

I2S pins (DIN, BCK, LCK) are configured in:
- `core/src/config.h` - default pins
- `core/src/config_local.h` - local overrides (copy `config_local.h.example` for your board)

Default pins (on typical RP2040/RP2350A builds, GPIO 26–29 remain free for on-chip ADC):
- DIN (data): GPIO 5
- LCK (word clock/LRCLK): GPIO 6
- BCK (bit clock): GPIO 7

Default peripherals do not conflict: OLED uses GPIO 2/3, WS2812 uses GPIO 16 (see `config.h`). See `config_local.h.example` for overrides.

### I2C Bus Configuration

I2C is initialized through a central bus layer on core0. Configure pins and baud rate in:
- `core/src/config.h` - defaults
- `core/src/config_local.h` - local overrides (copy `config_local.h.example`)

Use `I2C_BUS0_*` / `I2C_BUS1_*` to define buses, and `OLED_I2C_BUS_ID` / `MPR121_I2C_BUS_ID` to select which bus each device uses. Legacy `I2C_BUS_*` / `OLED_I2C_*` macros still map to bus 0.

## Flashing

Flash the firmware manually using picotool:

```bash
picotool load build/<patch_name>/firmware-build/rp2350_puredata_firmware.elf.uf2
picotool reboot
```

Or copy the `.uf2` file to the RP2350's mass storage device (if mounted).

## Patch API

The contract between Pd patches and firmware is defined in `core/src/patch_api.c` and documented here.

### MIDI

Use standard Pd MIDI objects in your patch: `[notein]`, `[ctlin]`, `[bendin]`, `[pgmin]`, `[touchin]`, `[polytouchin]`. The firmware sends USB MIDI into hvcc receivers with canonical argument order:

| Pd object   | hvcc receiver    | Message format (order)        |
|-------------|------------------|-------------------------------|
| `[notein]`  | `__hv_notein`    | (pitch, velocity, channel0)   |
| `[ctlin]`   | `__hv_ctlin`     | (value, cc, channel0)         |
| `[bendin]`  | `__hv_bendin`    | (bend14, channel0)            |
| `[pgmin]`   | `__hv_pgmin`     | (program, channel0)           |
| `[touchin]` | `__hv_touchin`   | (pressure, channel0)          |
| `[polytouchin]` | `__hv_polytouchin` | (pressure, note, channel0) |

`channel0` is 0..15. Reference: `third_party/hvcc/tests/src/test_midi.cpp`.

### Single entry point for send hook

**`hv_setSendHook()` is called in exactly one place: `patch_api_init(ctx)`.** No driver (ws2812, future I2C/encoders/display) must ever call `hv_setSendHook()`. This avoids "last one wins" when the project grows.

### @hv_param and naming (patch names, not C)

- **Inputs** (firmware → patch): Use **Daisy-style** names for potentiometers: `knob1`, `knob2`, `knob3`, `knob4` (e.g. `[r knob1 @hv_param 0 1 0]`). Potentiometers are **disabled by default**; enable in `core/src/config_local.h` by setting `POTS_BACKEND` to `POTS_BACKEND_ADC`. Configuration is by **ADC channel** (first channel `POTS_ADC_FIRST_CHANNEL`, count `POTS_COUNT`); physical pins are determined by the SDK and vary by chip (e.g. RP2040/RP2350A: channels 0–3 on GPIO 26–29; RP2350B: channels 0–7 on GPIO 40–47). Values are sent **only when they change** (poll interval `POTS_POLL_MS`, deadband `POTS_EPS`, and 1-pole ADC smoothing). If **POTS_BACKEND** is **NONE** or **POTS_COUNT** is 0, `knob*` receivers receive no values (they stay silent).
- **Other scalar inputs** (e.g. buttons): use `hw_*` names as needed; firmware pushes by hash.
- **Two classes of inputs**:
  - **Scalar state** (knob, pot, button state) → use **@hv_param** in the patch; firmware pushes by hash.
  - **Event/packet** ("button pressed", "I2C packet", "encoder +N") → use ordinary receivers/messages (not necessarily @hv_param) if it is not a UI parameter.
- **Commands from patch** (patch → firmware): Use **cmd_*** or **fw_*** as names in `[s ...]` (patch names only; C code may use any wrapper names). The command table (name → format → queue) is in `patch_api.c`.

### Send commands (LED)

Official API for LED control from the patch:

- **set_led_color** `(r g b)` — three floats (0–1 or 0–255). Sets all LEDs.
- **set_led_index** `(idx r g b)` — index plus three floats. Sets one LED.

These are the only supported send commands for LED; the table is extended in `patch_api.c` for future commands.

---

## Architecture at a Glance

The firmware uses **strict multicore separation**:
- **Core1 (audio core)**: Runs Heavy DSP processing (`hv_process*()`), owns the Heavy context, handles audio buffer production. No blocking I/O, no USB tasks, no `printf`.
- **Core0 (I/O core)**: Handles initialization, USB/MIDI, peripherals (WS2812 LEDs), and drains control queues.

Communication between cores uses two queues:
- `ctrl_queue` (core0 → core1): MIDI and control events (pushed via `patch_api_push_*` or `ctrl_push_hash_*`)
- `led_queue` (core1 → core0): LED commands from the patch send hook (routed in `patch_api.c`); core0 drains and calls `ws2812_*`

For complete architecture details, strict multicore rules, failure modes, and validation guidance, see [TECH.md](TECH.md).

## Project Structure

- **`pd-patches/`** - Pure Data patch source files
- **`core/`** - Core firmware project (I2S audio, Heavy integration, multicore)
- **`scripts/`** - Build automation scripts
- **`build/`** - Build output directory (created automatically)
- **`sdk/`** - SDK submodules (pico-sdk, pico-extras)
- **`third_party/`** - Third-party dependencies (heavylib, picotool)

## License

[Specify license]
