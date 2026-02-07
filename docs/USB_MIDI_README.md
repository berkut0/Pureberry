# USB MIDI Integration

## Overview

When USB MIDI support is enabled, the firmware enumerates as a **composite USB device**:
- **CDC** for debug serial (`printf`)
- **MIDI** for incoming MIDI messages

The firmware injects MIDI into hvcc in a way that keeps **standard Pure Data MIDI objects** working (`[notein]`, `[ctlin]`, etc.).

If you want a deeper, developer-focused explanation of the MIDI contract and hvcc receiver mapping, see `docs/MIDI.md` and `notes/HVCC_PATCH_API_REFACTOR.md`.

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

## Using MIDI in Pure Data patches

Use standard Pd MIDI objects in your patch:
- `[notein]`
- `[ctlin]`
- `[bendin]`
- `[pgmin]`
- `[touchin]`
- `[polytouchin]`

Do **not** use `[r notein]`, `[r ctlin]`, etc.

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

For a complete patch example, see `pd-patches/midi_synth_example.pd`.

## USB device information

See `core/src/usb/usb_descriptors.c` for exact values. At a glance:
- **Vendor ID**: 0xCafe
- **Product string**: "Pure Data Audio Device"
- **CDC interface string**: "Pure Data Debug"
- **MIDI interface string**: `USB_MIDI_DEVICE_NAME` (defaults to "Pure Data MIDI")

## Troubleshooting

### Device not recognized / no MIDI messages

- Ensure you built with `--usb-midi` (or `-D ENABLE_USB_MIDI=ON`).
- Verify the device appears as a MIDI input in your OS (Audio MIDI Setup / Device Manager / `aconnect -l`).
- Use a MIDI monitor/host app to confirm data is being sent.
- Use a simple patch and add `print` objects after `[notein]` / `[ctlin]`.

## Disabling USB MIDI

Build without `--usb-midi` (default behavior):

```bash
python scripts/build_firmware.py pd-patches/your_patch.pd
```

Or explicitly disable in manual CMake:

```bash
cmake -DENABLE_USB_MIDI=OFF ..
cmake --build .
```

## References

- TinyUSB documentation: `https://docs.tinyusb.org/`
- Pure Data MIDI objects: `https://puredata.info/docs/manuals/pd/x3.htm#s4`
