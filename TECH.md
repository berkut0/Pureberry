# Technical Documentation

## Project Overview

This project provides a build system for compiling Pure Data (Pd) patches into firmware for the Raspberry Pi RP2350 microcontroller. It leverages the RP2350's FPU and DSP capabilities to run real-time audio processing using the Heavy Compiler Collection (hvcc) to convert Pd patches into optimized C/C++ code.

## Architecture

### Components

1. **Pure Data Patches** (`pd-patches/`)
   - Source Pd patch files (`.pd` format)
   - Compiled using `hvcc` into C/C++ code
   - Supports Heavy-compatible abstractions from `heavylib` (e.g., `hv.osc~`, `hv.lfo`)

2. **Heavy Compiler Collection (hvcc)**
   - Python-based compiler that converts Pd patches to C/C++
   - Generates optimized audio processing code
   - Output: Heavy context interface and audio processing functions

3. **Core Firmware** (`core/`)
   - Base firmware project for RP2350
   - I2S audio output using pico-extras
   - Audio buffer management
   - Integration with Heavy-generated code

4. **Build Script** (`scripts/build_firmware.py`)
   - Orchestrates the entire build process
   - Compiles Pd patches with hvcc
   - Integrates Heavy code with core firmware
   - Builds final firmware using CMake/pico-sdk

5. **SDK Components** (`sdk/`)
   - `pico-sdk`: Raspberry Pi Pico SDK (version 2.2.0) as git submodule
   - `pico-extras`: Additional libraries including I2S audio support as git submodule

## Build Process

### Step-by-Step Flow

1. **PD Patch Compilation**
   ```
   hvcc <patch.pd> -o <build_dir> -n patch -p <search_paths>
   ```
   - Generates C/C++ code in `build/<patch_name>/c/`
   - Creates Heavy context interface with a fixed name: `Heavy_patch.h/cpp`
   - Generates audio processing functions
   - Search paths (`-p`) configured automatically for heavylib abstractions:
     - Uses vendored `third_party/heavylib` submodule when present
     - Optional custom paths via `HVCC_SEARCH_PATHS` environment variable

2. **Core Project Integration**
   - Core firmware project is used directly (not copied)
   - Heavy-generated sources are integrated via CMake manifest file
   - Build script generates `heavy_sources.cmake` manifest listing all Heavy C/C++ files
   - CMake includes the manifest via `-DHEAVY_SOURCES_FILE` variable
   - Firmware uses a fixed Heavy API name: `hv_patch_new()` / `hv_patch_free()`

3. **CMake Configuration**
   - Configures build system with pico-sdk
   - Links pico-extras for I2S audio
   - Sets up ARM cross-compilation toolchain
   - Generates build files using Ninja

4. **Firmware Compilation**
   - Compiles Heavy-generated C/C++ code
   - Links with pico-sdk and pico-extras libraries
   - Creates UF2 firmware file for RP2350

### Build Script Features

- **Clean**: With `--clean`, only patch artifacts (`c/`, `firmware-build*`) inside `build/<patch>/` are removed; the directory and log file are kept to avoid file locks on Windows. Default behavior is no clean unless `--clean` is passed.
- **Submodule Management**: Automatically detects and uses local SDK submodules
- **Error Handling**: Graceful handling of locked files on Windows (retries, fallback to `firmware-build-<timestamp>` when `firmware-build` is locked)
- **Flexible Output**: Supports custom output directories via `-o` option (default: `build/` in project root)
- **Logging Levels**: Configurable verbosity (quiet/normal/verbose/debug); log file `build/<patch>/build_firmware.log` gets full subprocess output only in verbose/debug; in normal/quiet the file contains only script messages at INFO level and above
- **Abstraction Search Paths**: Automatically configures hvcc search paths for heavylib abstractions
- **Context Integration**: Generates CMake manifest (`heavy_sources.cmake`) listing Heavy sources and includes it via `HEAVY_SOURCES_FILE` variable

### Feature Flags (Build-Time Options)

Defaults (when no flags are provided):
- `ENABLE_WS2812=ON` (opt-out)
- `ENABLE_OLED=OFF`
- `ENABLE_MPR121=OFF`
- `ENABLE_USB_MIDI=OFF`
- `ENABLE_I2C_DMA=OFF`

Build script flags (single argument per feature):
```bash
# Enable optional features
python scripts/build_firmware.py patch.pd --oled --mpr121 --usb-midi --i2c-dma

# Disable WS2812 (ON by default)
python scripts/build_firmware.py patch.pd --no-ws2812
```

You can always pass explicit CMake defines:
```bash
python scripts/build_firmware.py patch.pd -D ENABLE_OLED=ON -D ENABLE_WS2812=OFF
```

### USB Stack Contract (Current)

USB is unified and must stay this way:

- The firmware always uses TinyUSB device stack (`tinyusb_device`, `tinyusb_board`).
- `pico_stdio_usb` is not used by firmware targets.
- CDC is always enabled (`CFG_TUD_CDC=1`).
- `ENABLE_USB_MIDI` only toggles MIDI class (`CFG_TUD_MIDI`) and descriptor shape (CDC-only vs CDC+MIDI).

Runtime rules:

- USB stack servicing (`tud_task()`) runs continuously on core0 in the main loop.
- `usb_cdc_task()` runs on core0 in all builds.
- `usb_midi_task()` runs on core0 only when `ENABLE_USB_MIDI=ON`.
- TinyUSB callbacks must remain lightweight and non-blocking (no heavy I/O).

Implementation map:

- Build wiring: `core/CMakeLists.txt`
- TinyUSB config: `core/src/tusb_config.h`
- USB descriptors: `core/src/usb/usb_descriptors.c`
- CDC stdio bridge: `core/src/usb/usb_cdc.c`
- MIDI ingress: `core/src/usb/usb_midi.c`

### Logging System

The build script provides a unified logging system with multiple verbosity levels:

- **quiet** (`-q`): Only errors and critical warnings
- **normal** (default): Brief steps, warnings/errors visible, shows build progress messages
- **verbose** (`-v`): Real-time streaming output from hvcc/cmake, shows build progress `[X/115]`
- **debug** (`-d`): Full command details, paths, and all verbose output

**Log File**: The log is written to `build/<patch>/build_firmware.log`. In **verbose** and **debug** modes, the file contains the full output from subprocesses (file-level DEBUG). In **normal** and **quiet** modes, the file contains only script messages at INFO level and above (line-by-line cmake/ninja output is not saved to the file).

**Usage**:
```bash
python scripts/build_firmware.py patch.pd --log-level verbose
python scripts/build_firmware.py patch.pd -v  # alias
python scripts/build_firmware.py patch.pd -d   # debug mode
```

## Audio Processing Pipeline

### I2S Configuration

- **Hardware**: PCM5102A DAC (or compatible I2S DAC)
- **Pins** are configured in `core/src/config.h` (repo defaults) or `core/src/config_local.h` (local overrides; see `config_local.h.example`):
  - `PICO_AUDIO_I2S_DATA_PIN` — DIN (data)
  - `PICO_AUDIO_I2S_CLOCK_PIN_BASE` — first clock pin (the next one is `base+1`)
  - `PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED`: 0 = base→LRCLK (WS), base+1→BCLK; 1 = base→BCLK, base+1→LRCLK
- **Repo defaults**: DIN=GPIO5, base=GPIO6 (LRCLK=GPIO6, BCLK=GPIO7 when swap=0). Example alternative wiring is in `config_local.h.example`.
- **Format**: 16-bit signed PCM, stereo, 48 kHz

### Audio Flow

1. **Heavy Processing**
   - Heavy context processes audio in blocks of 64 samples per channel
   - Uses `hv_processInlineInterleaved()` for stereo interleaved format (L, R, L, R, ...)
   - Output: Float samples in range [-1.0, 1.0]

2. **Format Conversion**
   - Float to int16 conversion: `float_to_int16()`
   - Clamps to [-1.0, 1.0] range
   - Converts to 16-bit signed integers

3. **Buffer Management**
   - Audio buffer pool with 3 buffers
   - Each buffer: 64 samples per channel (128 total interleaved samples)
   - Producer-consumer pattern for I2S DMA

4. **I2S Output**
   - PIO-based I2S implementation from pico-extras
   - DMA transfers audio data to I2S hardware
   - Continuous streaming to PCM5102A DAC

### Multicore

The firmware uses strict multicore separation: audio runs on **core1** only; **core0** handles init, USB/MIDI, and peripherals (WS2812). This is the only supported execution mode. Communication is via `core/src/crosscore_bus.h`:

- **ctrl_queue (core0 → core1)**: MIDI and other control events. core0 pushes via `patch_api_push_*` (MIDI) or `crosscore_bus_ctrl_try_push_*` (generic); core1 drains at the start of each audio buffer and applies them to Heavy. Overflow policy: drop newest.
- **led_mailbox (core1 → core0)**: Pd send-hook LED color state (`set_led_color`). The send hook runs in the Heavy/audio context and only parses/publishes latest color; core0 consumes latest state in the main loop and performs the hardware update.

DMA/IRQ remain on core0; the buffer pool uses spinlocks so producer (core1) and consumer (DMA) are safe across cores. The full architectural rules are defined in the "Strict Multicore Rules" section below.

#### Strict Multicore Rules (Must Always Hold)

These rules define the architecture contracts that must be maintained for correct operation:

1. **Heavy context ownership**: Only **core1** may call any Heavy API functions:
   - `hv_process*()` / `hv_processInlineInterleaved()` - audio processing
   - `hv_sendMessageToReceiver*()` - control message injection (called via `crosscore_bus_ctrl_drain_to_heavy()`)
   - `hv_patch_new()` / `hv_patch_free()` - context lifecycle
   - **core0 must never call Heavy APIs directly**

2. **Audio core (core1) restrictions**: The audio core must avoid any operation that can block unpredictably or introduce jitter:
   - No blocking I/O (USB, I2C, SPI transactions)
   - No `printf()` or logging (use queues to forward to core0 if needed)
   - No `sleep()` or long delays
   - No dynamic memory allocation (`malloc`/`free`)
   - No long critical sections or spinlocks (except audio buffer pool, which is designed for cross-core use)

3. **Send hook execution context**: The send hook runs in the Heavy/audio context (RT). **Single entry point**: `hv_setSendHook()` is called in exactly one place — `patch_api_init(ctx)` in `patch_api.c`. No driver (ws2812, future I2C/encoders/display) must ever call `hv_setSendHook()`. The hook must be RT-safe: only minimal parsing and transport publish (currently: LED mailbox). Core0 in the main loop consumes transport state and performs the real work (I2C, GPIO, display). Do not call `ws2812_set_*()` or other hardware drivers directly from the send hook.

4. **I/O core (core0) responsibilities**: core0 handles all non-audio work:
   - USB stack servicing (`tud_task()`)
   - MIDI packet processing (pushes to `ctrl_queue`)
   - Peripheral control (WS2812 LED updates via mailbox consume + `ws2812_set_all()`)
   - Initialization (clocks, audio pool, Heavy context creation before launching core1)

5. **Transport semantics**:
   - `ctrl_queue`: core0 pushes, core1 drains once per audio buffer (before `hv_process*()`)
   - `led_mailbox`: core1 send hooks publish latest color, core0 consumes latest state in main loop
   - Overflow policy: **drop newest** applies to `ctrl_queue` only (`crosscore_bus.c`)

#### I2C Bus Architecture

I2C is managed through a dedicated bus layer (`core/src/drv/i2c_bus.[ch]`) and **must stay on core0**. The bus layer owns:
- GPIO mux + pullups, `i2c_init()` and controller resets
- bus recovery (SCL toggling + STOP)
- timeout-based transfers (no hard hangs)
- DMA arbitration (OLED refresh vs sensor reads)

Configuration is in `core/src/config.h` with local overrides in `config_local.h`:
- `I2C_BUS0_*` / `I2C_BUS1_*` define pins, instance, baud, timeouts
- `OLED_I2C_BUS_ID` / `MPR121_I2C_BUS_ID` select the bus per device

The core0 main loop calls `i2c_bus_poll()` to progress DMA transactions. Leaf drivers must not call `i2c_init()` directly.

#### Failure Modes & Backpressure

**Transport pressure**:
- `ctrl_queue` can overflow; new events are dropped (newest dropped policy).
- `led_mailbox` does not queue history; newest state overwrites older state.

- **ctrl_queue overflow**: MIDI events may be lost during bursts. Mitigation: rate-limit or coalesce continuous controls (CC) on core0 before pushing.
- **led_mailbox latest-wins**: intermediate LED colors may be skipped during bursts. This is expected and acceptable for visual feedback.

**Message drops**: Heavy's internal message queue (used by `hv_sendMessageToReceiver*()`) may also saturate. The `crosscore_bus_ctrl_try_push_*` / `patch_api_push_*` functions return `false` on overflow, allowing core0 to implement coalescing or rate limiting.

**Recommendations**:
- For continuous controls (knobs, sensors, CC streams): coalesce updates (keep latest value only) before pushing to `ctrl_queue`. Potentiometers (knob1..knob4) are configured by **ADC channel** (first channel **POTS_ADC_FIRST_CHANNEL**, count **POTS_COUNT**); physical pins come from the SDK (chip-dependent). They are only pushed when **POTS_BACKEND** is ADC and **POTS_COUNT** > 0; values are sent only on change (poll interval **POTS_POLL_MS**, deadband **POTS_EPS**, 1-pole smoothing **POTS_ALPHA**). Otherwise those receivers receive no values.
- For discrete events (note on/off): ensure queue depth is sufficient for expected burst rates
- Monitor queue pressure during development (add counters if needed)

#### Validation

Multicore changes should be validated under worst-case load, not only idle conditions:

1. **Baseline metrics**: Measure audio block processing time (DSP + conversion + buffer handoff) to establish headroom
2. **Stress testing**: Run worst-case USB/MIDI traffic and I2C polling while audio plays; confirm no audio underruns/glitches
3. **Queue backpressure**: Intentionally burst control events (e.g., dense CC streams) and verify overflow policy behaves as intended
4. **Long-run stability**: Run for extended periods (minutes/hours) to catch rare race conditions or buffer starvation

**Success criteria**:
- No buffer underruns or audible glitches under stress
- Consistent (low-variance) audio loop timing with safety margin relative to block duration (~1.33 ms at 48 kHz, 64 samples/channel)
- Control event latency stays within tolerance (typically 1 audio block)

## Hardware Requirements

### Target Hardware
- **Microcontroller**: Raspberry Pi RP2350 (RP2350-Zero board)
- **Audio DAC**: PCM5102A (I2S interface)
- **Connections**: I2S pins are configured in `config.h` / `config_local.h` (see I2S Configuration). Defaults: DIN→GPIO 5, LCK (LRCLK)→GPIO 6, BCK→GPIO 7. Copy `config_local.h.example` to `config_local.h` and set pins for your board.
- Common ground required

### Development Tools
- **Python 3.9+** with virtual environment
- **CMake** (pinned to `3.28.3` in `requirements.txt`; if system CMake is used, minimum `3.13+`)
- **Ninja** build system (installed via pip)
- **ARM GCC Toolchain** (arm-none-eabi-gcc 13.2+)
- **Host C/C++ compiler** (Clang 18+ recommended; required for Pico SDK host tools like `pioasm`)

### Platform Support Tiers

- **Supported**: Windows (active maintainer environment)
- **Best effort**: Linux, macOS
- **Untested**: other platforms/configurations

## Software Dependencies

### Python Packages
- `hvcc==0.15.0`: Pure Data to C/C++ compiler
- `ninja==1.13.0`: Build system
- `cmake==3.28.3`: CMake (installed via pip)

### SDK Components
- **pico-sdk 2.2.0+**: Core SDK for RP2350
- **pico-extras**: Additional libraries including:
  - `pico_audio`: Audio buffer management
  - `pico_audio_i2s`: I2S audio output driver

### Heavy Abstractions
- **heavylib**: Heavy-compatible Pure Data abstractions
  - Provided as git submodule in `third_party/heavylib`
  - Contains abstractions like `hv.osc~`, `hv.lfo`, `hv.filters`, etc.
  - Build script automatically configures search paths for hvcc
  - If submodule is unavailable, provide custom paths via `HVCC_SEARCH_PATHS`

## File Structure

```
rp2350-puredata/
├── core/                    # Core firmware project
│   ├── src/
│   │   ├── main.c          # Main firmware entry point
│   │   ├── config.h        # Firmware config (I2S pins, USB MIDI, etc.)
│   │   ├── config_local.h.example  # Example for local overrides (copy to config_local.h)
│   │   └── pico_config.h   # Pico SDK global config (includes config.h)
│   ├── CMakeLists.txt      # CMake configuration
│   ├── pico_sdk_import.cmake
│   └── pico_extras_import.cmake
├── pd-patches/             # Pure Data patch files
├── scripts/
│   └── build_firmware.py   # Main build script
├── sdk/                     # SDK submodules
│   ├── pico-sdk/           # Raspberry Pi Pico SDK
│   └── pico-extras/        # Additional libraries
├── third_party/            # Third-party dependencies
│   └── heavylib/           # Heavy-compatible abstractions (git submodule)
├── build/                   # Build output directory
├── requirements.txt         # Python dependencies
└── README.md               # Project overview
```

## Key Technical Details

### Heavy Integration

Heavy generates a context interface for each patch:
- `hv_patch_new(double sampleRate)`: Creates context (project name is fixed to `patch`)
- `hv_processInlineInterleaved()`: Processes audio blocks
- `hv_patch_free()`: Cleans up context

The context processes audio in blocks of 64 samples per channel, matching Heavy's default block size.

**Important**: The firmware code includes `Heavy_patch.h` and creates the Heavy context via `hv_patch_new()`. The build script generates a CMake manifest file (`heavy_sources.cmake`) that lists all Heavy-generated source files, and CMake includes this manifest via the `HEAVY_SOURCES_FILE` variable. The core project is used directly without copying.

### Heavy-Firmware Message Exchange

Heavy supports bidirectional communication between compiled Pd patches and firmware code via send hooks and receive messages.

**Send Hooks (Pd → Firmware)**:
- Pd patches use `[s <name> @hv_event]` objects to send messages to firmware
- hvcc generates deterministic hash identifiers for each send channel name
- Firmware registers a callback via `hv_setSendHook(heavy_context, hv_send_hook)` (C API) or `HeavyContextInterface::setSendHook()` (C++ API)
- Hook is called synchronously during audio processing when send is triggered
- Hook receives: send channel name (string), hash (uint32_t), and message payload (HvMessage*)
- Message types: bang (event), float, symbol (string), list (array)
- Hash computation: use `hv_stringToHash("send_name")` to compute hash from send channel name
- Identification: use hash comparison for performance (`sendHash == hv_stringToHash("name")`), or string comparison for flexibility

**Receive Messages (Firmware → Pd)**:
- Firmware can send messages to Pd patches using `hv_sendMessageToReceiver()`
- Requires receiver object `[r <name>]` in Pd patch
- Messages can contain float values, bangs, or symbols
- Useful for parameter updates, synchronization, or feedback

**Message Flow**:
1. Pd patch executes send object → hvcc-generated code calls send hook
2. Firmware hook function receives message with channel identifier
3. Firmware processes message (e.g., control GPIO, update state)
4. Optional: Firmware sends response via receive message to Pd patch

**Use Cases**: LED control, parameter updates, event synchronization, hardware feedback, visual indicators tied to audio processing.

### Audio Buffer Format

- **Format**: `AUDIO_BUFFER_FORMAT_PCM_S16` (16-bit signed PCM)
- **Channels**: 2 (stereo)
- **Sample Rate**: 48 kHz
- **Buffer Size**: 64 samples per channel (128 total interleaved samples)
- **Interleaved Format**: [L, R, L, R, ...]

### Memory Layout

- Audio buffers allocated in RAM
- Heavy context and processing in RAM
- Code and constants in flash
- DMA transfers from RAM to I2S hardware

### Performance Considerations

- **Block Processing**: 64 samples per block = ~1.33 ms latency at 48 kHz
- **DMA**: Hardware DMA transfers reduce CPU load
- **PIO I2S**: Software I2S implementation using PIO state machine
- **FPU**: RP2350's FPU enables efficient float processing

## Build Configuration

### CMake Variables

- `PICO_BOARD=waveshare_rp2350_zero`: Target board
- `PICO_SDK_PATH`: Path to pico-sdk (auto-detected from submodule)
- `PICO_EXTRAS_PATH`: Path to pico-extras (auto-detected from submodule)
- `PICO_TOOLCHAIN_PATH`: ARM GCC toolchain path (from environment)

### Compiler Settings

- **C Standard**: C11
- **C++ Standard**: C++17
- **Optimization**: -O3 (Release mode)
- **Target**: ARM Cortex-M33 (RP2350)

## Troubleshooting

### Common Issues

1. **No audio output**
   - Check I2S pin connections
   - Verify PCM5102A power and ground
   - Check audio buffer pool initialization

2. **Distorted audio**
   - Verify correct use of `hv_processInlineInterleaved()`
   - Check sample_count interpretation (per channel vs total)
   - Verify float-to-int16 conversion

3. **Build failures**
   - Ensure all submodules are initialized: `git submodule update --init --recursive`
   - Check ARM toolchain is in PATH
   - Verify Clang version (18+ recommended)
   - For heavylib abstractions: Ensure `third_party/heavylib` submodule is initialized, or set `HVCC_SEARCH_PATHS` environment variable

4. **No audio with hv.osc~ or other heavylib abstractions**
   - Verify heavylib submodule is initialized: `git submodule update --init --recursive`
   - Check that build script reports "hvcc search paths" in verbose output
   - Ensure Heavy context is initialized in `core/src/main.c` (it should call `hv_patch_new()`)
   - If using custom abstraction paths, set `HVCC_SEARCH_PATHS` environment variable (semicolon-separated on Windows)

5. **Locked files on Windows**
   - Close any programs accessing build directory
   - Build script handles this gracefully with warnings

6. **Heavy context initialization issues**
   - If `heavy_context` is NULL, check that `hv_patch_new()` returns non-NULL and that `Heavy_patch.h` is present in `build/<patch>/c/`
   - Confirm hvcc was invoked with `-n patch` (fixed Heavy project name)
   - Verify `heavy_sources.cmake` manifest was generated and contains Heavy source files

## Heavy Abstractions Support

### heavylib Integration

The project supports Heavy-compatible abstractions from the `heavylib` library, which provides abstractions like:
- `hv.osc~`: Oscillators (sine, saw, square, triangle)
- `hv.lfo`: Low-frequency oscillators
- `hv.filters`: Various filter types

**Setup**:
1. heavylib is included as a git submodule in `third_party/heavylib`
2. Initialize with: `git submodule update --init --recursive`
3. Build script automatically configures hvcc search paths

**Search Path Resolution**:
1. Vendored: Uses `third_party/heavylib` submodule when present
2. Custom: Additional paths via `HVCC_SEARCH_PATHS` environment variable (semicolon-separated on Windows)

**Note**: hvcc does not search recursively, so the build script explicitly adds subdirectories like `hv.osc`, `hv.lfo`, `hv.filters` to the search paths.

## Future Improvements

- Audio input support (ADC or I2S input)
- USB audio device mode
- Real-time parameter control
- Multiple patch support
- Performance profiling tools

**Note**: USB MIDI support is already available (CMake option `ENABLE_USB_MIDI`, see `config.h` and USB MIDI docs).
