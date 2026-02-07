# USB MIDI Guide

## Overview

When USB MIDI support is enabled, the firmware enumerates as a **composite USB device**:
- **CDC** for debug serial (`printf`)
- **MIDI** for incoming MIDI messages

MIDI is injected into the hvcc patch in a way that keeps **standard Pure Data MIDI objects** working (`[notein]`, `[ctlin]`, etc.).

## Enabling USB MIDI

USB MIDI is **disabled by default**.

### Build script (recommended)

```bash
python scripts/build_firmware.py pd-patches/your_patch.pd --usb-midi
```

### Alternative: explicit CMake define

```bash
python scripts/build_firmware.py pd-patches/your_patch.pd -D ENABLE_USB_MIDI=ON
```

### Manual CMake (advanced)

```bash
cmake -DENABLE_USB_MIDI=ON ..
cmake --build .
```

Note:
- The build script does not flash automatically. Flash the UF2 manually (picotool or drag-and-drop).
- USB is always handled by TinyUSB (Pico SDK integration). `--usb-midi` / `ENABLE_USB_MIDI` only toggles the MIDI interface; CDC is always present.

## Using MIDI in Pure Data patches

Use standard Pd MIDI objects in your patch:
- `[notein]`
- `[ctlin]`
- `[bendin]`
- `[pgmin]`
- `[touchin]`
- `[polytouchin]`

Do **not** use `[r notein]`, `[r ctlin]`, etc. Those are legacy/incorrect for this project's current hvcc integration.

### Example: note-controlled oscillator

```pd
[notein]
|     \
|      [/ 127]          // velocity 0..127 -> 0..1
|       |
[mtof]  |
|       |
[hv.osc~ sine]
|
[*~]
|
[dac~]
```

## Implementation overview (for firmware developers)

### Data flow

- **core0**:
  - services USB (`tud_task()`)
  - parses incoming USB MIDI packets (`usb_midi_task()`)
  - pushes events into `ctrl_queue` via `patch_api_push_*()` (non-blocking, overflow returns `false`)

- **core1**:
  - drains `ctrl_queue` once per audio buffer (`multicore_drain_ctrl()`)
  - injects events into Heavy using hvcc-standard receiver hashes (`__hv_*`)

The `__hv_*` names are an implementation detail; patch authors should use the Pd MIDI objects above.

### Canonical hvcc receiver mapping (implementation detail)

This is the receiver contract used by hvcc (reference: `third_party/hvcc/tests/src/test_midi.cpp`):

| Pd object | hvcc receiver | Argument order |
|---|---|---|
| `[notein]` | `__hv_notein` | (pitch, velocity, channel0) |
| `[ctlin]` | `__hv_ctlin` | (value, cc, channel0) |
| `[bendin]` | `__hv_bendin` | (bend14, channel0) |
| `[pgmin]` | `__hv_pgmin` | (program, channel0) |
| `[touchin]` | `__hv_touchin` | (pressure, channel0) |
| `[polytouchin]` | `__hv_polytouchin` | (pressure, note, channel0) |

`channel0` is 0..15.

## Troubleshooting

### Windows: CDC (COM port) does not enumerate (Code 10)

See `docs/USB_DEBUG.md`.

### No MIDI messages received

- Ensure you built with `--usb-midi` (or `-D ENABLE_USB_MIDI=ON`).
- Verify the device shows up as a MIDI input in your OS.
- Use a simple patch and add `print` objects after `[notein]` / `[ctlin]`.

## References

- TinyUSB documentation: `https://docs.tinyusb.org/`
- Pure Data MIDI objects: `https://puredata.info/docs/manuals/pd/x3.htm#s4`
