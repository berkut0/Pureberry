# USB Debug (CDC + MIDI)

This project uses **TinyUSB (via Pico SDK)** for USB in all builds.
The `--usb-midi` / `ENABLE_USB_MIDI` option only toggles the **MIDI interface**. **CDC is always present**.

## Quick checks

- **CDC-only build**: device should show up as a **COM port** (Windows: *Ports (COM & LPT)*).
- **CDC+MIDI build** (`--usb-midi`): device should show up as **COM port + MIDI device**.
- Windows caches drivers aggressively (per VID/PID/interface set). Descriptor changes may require removing old device instances.

## CDC: COM port exists but looks "silent"

TinyUSB's CDC output is gated by **DTR**: the host must open the port.

Current firmware does not implement a CDC command protocol. If nothing is printed after you open the port, this is expected.

## Windows: Code 10 / no COM port

Symptoms:
- Device Manager shows a broken USB/composite device (**Code 10**).
- No COM port is created.

Common causes & actions:
- **Stale driver cache** from earlier descriptor variants:
  - Unplug device.
  - Device Manager -> View -> Show hidden devices -> remove old instances of the device (and/or use USBDeview).
  - Replug (try a different USB port).
- **Bad cable**: ensure it's a data cable.
- **USB servicing starvation**: verify the firmware continuously calls `tud_task()` on core0 and does not block core0 for long periods.

## Firmware pointers

- USB init + servicing loop: `core/src/main.c`
- TinyUSB config: `core/src/tusb_config.h`
- Descriptors (CDC-only vs CDC+MIDI): `core/src/usb/usb_descriptors.c`
- CDC stdio integration: `core/src/usb/usb_cdc.c`
