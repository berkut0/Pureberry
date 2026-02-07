# USB Debug (CDC + MIDI)

This project uses **TinyUSB (via Pico SDK)** for USB in all builds.
The `--usb-midi` / `ENABLE_USB_MIDI` option only toggles the **MIDI interface**. **CDC is always present**.

## Quick checks

- **CDC-only build**: device should show up as a **COM port** (Windows: *Ports (COM & LPT)*).
- **CDC+MIDI build** (`--usb-midi`): device should show up as **COM port + MIDI device**.
- If you are iterating on descriptors, remember that **Windows caches drivers aggressively** (per VID/PID/interface set).

## CDC: COM port exists but looks "silent"

TinyUSB's CDC output is gated by **DTR**: the host must open the port.

What to do:
- Open the port in a terminal (PuTTY, TeraTerm, `screen`, etc.).
- The firmware prints a one-time banner on connect: `rp2350-puredata CDC ready`.
- Try sending `ping` + Enter; you should get `pong` (plus an echo of what you typed).
- `help` shows available commands.
- `reboot` performs a **soft reboot** of the patch runtime (keeps USB/COM connected).

If you get echo + `pong`, CDC RX/TX is alive and the issue is in higher-level logic (or expectations about when `printf` appears).

## Windows: Code 10 / no COM port

Symptoms:
- Device Manager shows a broken USB/composite device (**Code 10**).
- No COM port is created.

Common causes & actions:
- **Stale driver cache** from earlier descriptor variants:
  - Unplug device.
  - Device Manager → *View → Show hidden devices* → remove old instances of the device (and/or use USBDeview).
  - Replug (try a different USB port).
- **Bad cable**: ensure it's a data cable.
- **USB servicing starvation**: verify the firmware continuously calls `tud_task()` on core0 and does not block core0 for long periods.

## Firmware pointers (where to look)

- USB init + servicing loop: `core/src/main.c`
- TinyUSB config: `core/src/tusb_config.h`
- Descriptors (CDC-only vs CDC+MIDI): `core/src/usb/usb_descriptors.c`
- CDC task (echo/commands + periodic flush): `core/src/usb/usb_cdc.c`
