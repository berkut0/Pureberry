<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/logo_light.png">
  <source media="(prefers-color-scheme: light)" srcset="assets/logo_dark.png">
  <img src="assets/logo_dark.png" alt="RP2350 Pure Data" width="480">
</picture>

</div>

# Pureberry (RP2350 Pure Data Build System)

Build system for compiling Pure Data (Pd) patches into firmware for Raspberry Pi RP2350 via hvcc.

## A note from the author

When I started the project in late 2023, I tried using RP2040 chips and was convinced that I was the first person to try compiling Pure Data on an ARM chip. This valuable experiment showed that the lack of a floating-point unit (FPU) practically rules out patches with more than four to six oscillators, and that it made no sense to develop the project further. I didn’t know at the time that the talented Daisy team had already spent years developing their platform — I only found out while working on the RP2040 firmware. If you don't enjoy tinkering, Daisy boards will likely suit you much better, as they offer an excellent out-of-the-box experience. However, if you enjoy getting your hands dirty with software and hardware tools, this is the place for you.

Later, the RP2350 was released, and I decided to try again. Before I knew it, I was maintaining a fairly complex project for compiling Pure Data patches. Creating working firmware from Pure Data patches does require some persistence, but I decided to publish it so that anyone can give it a try. Together with a community of like-minded individuals, I believe that we can make the firmware and build process highly simple and clear. Raspberry Pi chips are widely used and there are plenty of solid boards with optional PSRAM, which is strongly recommended for certain applications. With this project, you can build your own synthesizers based on Pure Data patches using a cheap and widely available chip.

I test on a board that you can replicate on a breadboard:

- **RP2350-Zero** (Waveshare)
- **PCM5102** module (I2S DAC)
- **I2C 128×64 OLED** display
- **Three buttons** for UI control
- **Four potentiometers** (all ADC channels on RP2350; RP2350B has eight)
- **MPR121** for feeding events into the patch
- **3.5 mm TRS jacks** for MIDI in/out (support in progress)

A 240 MHz profile is available; the chip can also be overclocked to 600 MHz, which makes the whole endeavour with these parts very attractive. So far, no other overclocking profiles have been implemented or tested.

## Description

This project compiles Pure Data patches into firmware for the RP2350 microcontroller, which features FPU and DSP capabilities suitable for real-time audio processing generated from Pd patches.

The firmware is highly configurable: build-time options and `config.h` / `config_local.h` let you adapt it to almost any scenario (which components you have or omit). A minimal setup is **any RP2350 board plus any I2S DAC**; the PCM510x family (e.g. PCM5102) works very well.

### Writing patches for this firmware

[PlugData](https://plugdata.org/) is recommended for authoring patches: it ships with **Heavy (Hv) nodes**, which are the objects supported by hvcc and used to generate the firmware DSP. Turn on **Compile Mode** in PlugData so the editor clearly marks objects that are not supported for compilation; that way you avoid building only to find unsupported nodes at compile time. The Heavy/hvcc backend has limitations and does not support every Pd object; for unsupported vanilla objects see the [hvcc documentation](https://github.com/Wasted-Audio/hvcc/blob/main/docs/10.unsupported_vanilla_objects.md).

## Requirements

- **Python 3.9+**
- **Python dependencies** — `python -m pip install -r requirements.txt` installs pinned `hvcc`, `ninja`, and `cmake` versions; with the project venv you do not need to install CMake or Ninja separately.
- **hvcc source of truth** — build script runs `hvcc` from the active environment (`PATH`, recommended from project `venv`). `third_party/hvcc` is kept as vendored source/tests reference, not as the executable used by the build script.
- **Raspberry Pi Pico SDK** (for RP2350) - included as git submodule
- **CMake** (pinned to `3.28.3` in `requirements.txt`; if you use system CMake instead, minimum `3.13+`)
- **Host C/C++ compiler** (needed for Pico SDK host tools like `pioasm`; e.g. LLVM Clang or Visual Studio Build Tools)
- **ARM GCC toolchain** (arm-none-eabi-gcc)
- **picotool** (optional, for manual flashing) - included as git submodule

Support tiers:
- **Supported**: Windows (actively used/tested by maintainer)
- **Best effort**: Linux, macOS (community testing welcome)
- **Untested**: other platforms/configurations

## Installation

After cloning the repository, initialize git submodules:

```bash
git submodule update --init --recursive
```

Install Python dependencies:

```bash
python -m pip install -r requirements.txt
```

Dependency pin policy:
- `requirements.txt` is the reproducible baseline for public builds.
- Version bumps should be intentional and accompanied by matching README/TECH updates.

Recommended for all platforms: use a repository-local virtual environment to keep tool versions isolated and predictable (`cmake`, `ninja`, `hvcc`):

```powershell
python -m venv venv
.\venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

```bash
python -m venv venv
source venv/bin/activate
python -m pip install -r requirements.txt
```

If your machine has multiple global Python/CMake/Ninja installs, prefer launching builds from the activated venv shell and keep IDE/toolchain paths explicit in workspace settings. CMake and Ninja from pip are used for the build; only ARM GCC and a host C/C++ compiler must be installed separately (see below).

### Toolchains (ARM GCC and host compiler)

The build needs **arm-none-eabi-gcc** and **arm-none-eabi-g++** on `PATH` or under `PICO_TOOLCHAIN_PATH` (the toolchain root; both the build script and CMake look in `PICO_TOOLCHAIN_PATH/bin`). Set `PICO_TOOLCHAIN_PATH` in the same environment where you run the build (e.g. in your terminal or in the shell that runs the script). On Windows, install the [GNU Arm Embedded Toolchain](https://developer.arm.com/downloads/-/gnu-rm), then add its `bin` directory to `PATH` or set `PICO_TOOLCHAIN_PATH` to the toolchain root. On Linux, install the `gcc-arm-none-eabi` package (Debian/Ubuntu) or unpack the official tarball and set `PATH` or `PICO_TOOLCHAIN_PATH`. On macOS, use Homebrew (`arm-none-eabi-gcc`) or the official package and put `bin` on `PATH` or set `PICO_TOOLCHAIN_PATH`.

The Pico SDK builds host tools (e.g. pioasm) during configure. The script prefers `gcc`/`g++`; if missing, CMake can use `clang++` or MSVC. On Windows, install Visual Studio Build Tools with "Desktop development with C++" or full Visual Studio, and run builds from a Developer Command Prompt (or ensure `cl` is on `PATH`); or install LLVM/Clang. On Linux, install `build-essential`. On macOS, install Xcode Command Line Tools (`xcode-select --install`) or full Xcode. Missing ARM toolchain (`arm-none-eabi-gcc/g++`) fails Preflight. Missing host C++ compiler is reported as a warning, and CMake may still fail later if it cannot select a usable host toolchain.

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

# Force 240 MHz OC profile (explicit override from build script)
python scripts/build_firmware.py pd-patches/your_patch.pd --overclocked
```

You can always pass explicit CMake defines:
```bash
python scripts/build_firmware.py pd-patches/your_patch.pd -D ENABLE_OLED=ON -D ENABLE_WS2812=OFF
```

Clock behavior:
- If `FW_SYS_CLOCK_PROFILE` is defined in `core/src/config.h` / `core/src/config_local.h`, firmware uses it.
- `--overclocked` explicitly overrides this at build time to `FW_SYS_CLOCK_PROFILE_OC_240MHZ`.
- With `--overclocked`, script also defaults `PICO_FLASH_SPI_CLKDIV=4` unless you pass your own `-D PICO_FLASH_SPI_CLKDIV=...`.

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
- **Commands from patch** (patch → firmware): Use **cmd_*** or **fw_*** as names in `[s ...]` (patch names only; C code may use any wrapper names). The command table (name → format → handler) is in `patch_api.c`.

### Send commands (LED)

Official API for LED control from the patch:

- **set_led_color** `(r g b)` — three floats (0–1 or 0–255). Sets all LEDs.

This is the only supported LED send command in firmware.

---

## Architecture at a Glance

The firmware uses **strict multicore separation**:
- **Core1 (audio core)**: Runs Heavy DSP processing (`hv_process*()`), owns the Heavy context, handles audio buffer production. No blocking I/O, no USB tasks, no `printf`.
- **Core0 (I/O core)**: Handles initialization, USB/MIDI, peripherals (WS2812 LEDs), and drains control queues.

Communication between cores uses:
- `ctrl_queue` (core0 → core1): MIDI/control events (pushed via `patch_api_push_*` / `crosscore_bus_ctrl_try_push_*`)
- `led_mailbox` (core1 → core0): latest LED color published by patch send hook; core0 consumes latest state and applies `ws2812_set_all(...)`

For complete architecture details, strict multicore rules, failure modes, and validation guidance, see [TECH.md](TECH.md).

## Project Structure

- **`pd-patches/`** - Pure Data patch source files
- **`core/`** - Core firmware project (I2S audio, Heavy integration, multicore)
- **`scripts/`** - Build automation scripts
- **`build/`** - Build output directory (created automatically)
- **`sdk/`** - SDK submodules (pico-sdk, pico-extras)
- **`third_party/`** - Third-party dependencies (heavylib, hvcc sources/tests, u8g2, pico-mpr121, multibutton, picotool)

## License

This project is licensed under the MIT License. See `LICENSE`.
