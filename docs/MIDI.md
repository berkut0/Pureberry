# MIDI Guide

## Scope

This is the user-facing guide for MIDI usage in patches.

Current transport:
- USB MIDI input from host to device.

Future transports (for example native MIDI over current loop) may be added later.
Patch-side usage should remain based on standard Pd MIDI objects.

## USB Behavior

- USB stack is TinyUSB in all builds.
- CDC is always present.
- `--usb-midi` (or `-D ENABLE_USB_MIDI=ON`) adds the MIDI interface.

Enumeration:
- Default build: CDC only.
- MIDI build: CDC + MIDI composite device.

## Enabling MIDI

USB MIDI is disabled by default.

Build script (recommended):

```bash
python scripts/build_firmware.py pd-patches/your_patch.pd --usb-midi
```

Explicit CMake define:

```bash
python scripts/build_firmware.py pd-patches/your_patch.pd -D ENABLE_USB_MIDI=ON
```

Manual CMake:

```bash
cmake -DENABLE_USB_MIDI=ON ..
cmake --build .
```

## Using MIDI in Pure Data Patches

Use standard Pd MIDI objects:
- `[notein]`
- `[ctlin]`
- `[bendin]`
- `[pgmin]`
- `[touchin]`
- `[polytouchin]`

Example:

```pd
[notein]
|     \
|      [/ 127]
|       |
[mtof]  |
|       |
[hv.osc~ sine]
|
[*~]
|
[dac~]
```

Example patch: `pd-patches/midi_synth_example.pd`.

## Troubleshooting

No MIDI messages:
- Check that firmware was built with `--usb-midi`.
- Check that OS sees the device as a MIDI input.
- Use a MIDI monitor/host application to verify outgoing host MIDI.
- Add `print` after `[notein]` / `[ctlin]` in the patch to confirm reception.

## References

- TinyUSB: `https://docs.tinyusb.org/`
- Pure Data MIDI objects: `https://puredata.info/docs/manuals/pd/x3.htm#s4`
