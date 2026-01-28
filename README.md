# RP2350 Pure Data Build System

Build system for compiling Pure Data (Pd) patches into firmware for Raspberry Pi RP2350 via hvcc.

## Description

This project compiles Pure Data patches into firmware for the RP2350 microcontroller, which features FPU and DSP capabilities suitable for real-time audio processing generated from Pd patches.

## Requirements

- **Python 3.9+**
- **hvcc** (`pip install -r requirements.txt`)
- **Raspberry Pi Pico SDK** (for RP2350) - included as git submodule
- **CMake 3.13+**
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

### Logging Levels

The build script supports multiple verbosity levels:

- `--log-level normal` (default) - brief steps, warnings/errors visible
- `--log-level verbose` or `-v` - real-time build progress streaming
- `--log-level debug` or `-d` - full commands and paths
- `--log-level quiet` or `-q` - errors only

Build log is saved to `build/<patch>/build_firmware.log` (full subprocess output in verbose/debug modes; script messages only in normal/quiet).

## Configuration

### I2S Pin Configuration

I2S pins (DIN, BCK, LCK) are configured in:
- `core/src/config.h` - default pins
- `core/src/config_local.h` - local overrides (copy `config_local.h.example` for your board)

Default pins:
- DIN (data): GPIO 26
- BCK (bit clock): GPIO 28
- LCK (word clock/LRCLK): GPIO 27

See `config_local.h.example` for an example configuration (e.g., DIN=5, LRCLK=6, BCLK=7).

## Flashing

Flash the firmware manually using picotool:

```bash
picotool load build/<patch_name>/firmware-build/rp2350_puredata_firmware.elf.uf2
picotool reboot
```

Or copy the `.uf2` file to the RP2350's mass storage device (if mounted).

## Architecture at a Glance

The firmware uses **strict multicore separation**:
- **Core1 (audio core)**: Runs Heavy DSP processing (`hv_process*()`), owns the Heavy context, handles audio buffer production. No blocking I/O, no USB tasks, no `printf`.
- **Core0 (I/O core)**: Handles initialization, USB/MIDI, peripherals (WS2812 LEDs), and drains control queues.

Communication between cores uses two queues:
- `ctrl_queue` (core0 → core1): MIDI and control events
- `led_queue` (core1 → core0): LED commands from Pd send hooks

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
