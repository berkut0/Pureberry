# USB Audio (PC → RP2350) implementation notes

Date: 2026-01-31

## Goal

Expose **PC audio** (USB Audio OUT from the host) to the firmware as the **audio input** for Heavy/hvcc, so Pure Data patches can read it via `adc~` and route/process it to `dac~` (I2S output).

## What was implemented

- **USB Audio Device mode** (TinyUSB UAC2 “speaker” / host → device) with explicit feedback.
- **Core0 receive path**: TinyUSB audio RX FIFO → lock-free ring buffer.
- **Core1 DSP integration**: ring buffer → float conversion → `hv_processInlineInterleaved()` input buffer (so `adc~` works).
- **On-device diagnostics** on OLED (USB sample rate, alt-setting, streaming, buffer stats, last control request).

## Key code locations

- USB descriptors (CDC + optional MIDI + optional Audio):
  - `core/src/usb/usb_descriptors.c`
- TinyUSB config:
  - `core/src/tusb_config.h`
- USB audio runtime (ring buffer + TinyUSB callbacks):
  - `core/src/usb/usb_audio.c`
  - `core/src/usb/usb_audio.h`
- DSP integration (core1 reads USB audio frames and feeds Heavy input):
  - `core/src/main.c`
- OLED diagnostics UI:
  - `core/src/dev/oled.c`
  - `core/src/dev/u8g2_pico.c`

## The blocker (why audio did not work before)

When Windows selected the audio device, the firmware rebooted due to a pico-sdk `panic()` inside TinyUSB’s Raspberry Pi USB controller driver:

- Panic text: `"ep %02X was already available"`
- Source: `sdk/pico-sdk/lib/tinyusb/src/portable/raspberrypi/rp2040/rp2040_usb.c` (`_hw_endpoint_buffer_control_update32`)

Meaning: an endpoint transfer was queued while the endpoint DPRAM buffer-control `AVAIL` bit was still set (stale endpoint state / double-queue).

This prevented Windows from successfully switching the streaming interface to **alt=1**, so isoch OUT transfers never started.

## What change made audio start working

Audio became audible after removing that crash path and letting Windows start streaming:

1. **TinyUSB OSAL switched to Pico** (`OPT_OS_PICO`) to get proper synchronization primitives:
   - `core/src/tusb_config.h`
2. **Explicit endpoint cleanup on streaming stop (alt=0)**:
   - Clears the audio endpoint DPRAM buffer-control registers and disables the EP control registers for the audio EPs.
   - Implemented in `tud_audio_set_itf_close_EP_cb()`:
     - `core/src/usb/usb_audio.c`

Once the device stopped rebooting on selection, Windows could set **alt=1**, `usb_audio_task()` started receiving PCM frames, and core1 began feeding them into Heavy (`adc~`).

## Crash diagnostics (used to find the root cause)

To debug early USB failures without relying on CDC logging, a crash-capture mechanism was added:

- Stores minimal crash/panic info in watchdog scratch registers and reboots.
- Displays the info on OLED after reboot.
- Files:
  - `core/src/crash.c`
  - `core/src/crash.h`
  - `core/src/pico_config.h`

This was used to confirm the failure was a **panic** (not a HardFault) and to map the panic PC to TinyUSB’s driver.

## How to build the firmware (example)

Build a Pd patch as firmware (Windows PowerShell):

`.\venv\Scripts\Activate.ps1; python scripts/build_firmware.py pd-patches/adc_to_dac.pd -v -D ENABLE_USB_AUDIO=ON`

Output UF2:

`build/adc_to_dac/firmware-build/rp2350_puredata_firmware.elf.uf2`

## Current status

- USB audio input works (host → device).
- Crackle/distortion debugging focused on **rate control** (USB feedback) and avoiding long stalls on core0.

Mitigations added in firmware:

- **Underrun counter** (OLED shows `u...` while streaming) to confirm whether crackle correlates with missing frames:
  - `core/src/usb/usb_audio.c`
  - `core/src/usb/usb_audio.h`
  - `core/src/dev/oled.c`
- **Service USB-audio during OLED I2C transfers** to reduce USB FIFO starvation while pushing the framebuffer:
  - `core/src/dev/u8g2_pico.c`
- Increased buffering headroom:
  - deeper TinyUSB audio OUT SW FIFO: `core/src/tusb_config.h`
  - higher inter-core target fill when OLED is enabled: `core/src/config.h`
- **Core1 input prefill**:
  - do not consume from the ring until it is prefilled to `USB_AUDIO_TARGET_FILL_FRAMES`
  - implemented in `core/src/main.c`

## Update: explicit feedback based on ring fill

TinyUSB supports a “FIFO count” feedback method, but this firmware moves audio frames out of TinyUSB’s FIFO into a separate inter-core ring. Using FIFO-based feedback in that setup can cause the host rate control to fight the wrong buffer (FIFO vs ring), leading to ring fill swings and audible glitches.

The firmware now uses **application-provided explicit feedback**:

- `tud_audio_feedback_params_cb()` sets `AUDIO_FEEDBACK_METHOD_DISABLED` so TinyUSB does not overwrite feedback values.
- core0 computes feedback from the **inter-core ring fill level** and calls `tud_audio_fb_set()` regularly.
- feedback is seeded on stream start (alt setting != 0) so the feedback endpoint starts transmitting immediately.

Files:

- `core/src/usb/usb_audio.c`
- `core/src/usb/usb_audio.h`

OLED streaming diagnostics now include the current feedback value (`fb..` in 16.16 frames per USB frame), alongside ring fill (`r..`) and underrun count (`u..`).

## Update: OLED (I2C) causing periodic audio glitches

Symptom observed on hardware:

- USB audio works, but the audio stream periodically “slips” / crackles.
- The glitch period matches the OLED refresh period (e.g. ~50 ms at 20 FPS, ~500 ms at 2 FPS).

Root cause hypothesis:

- A full SSD1306 framebuffer push (`u8g2_SendBuffer()` → ~1024 bytes over I2C) creates a burst of work on core0.
- During that burst, USB isoch scheduling / draining can stall long enough to lose some OUT packets or drain the inter-core ring unevenly, producing discontinuities (phase jumps / short zero gaps).

Mitigation implemented:

- While USB audio is streaming, the OLED task no longer pushes the full framebuffer.
- Instead it draws a minimal **streaming status HUD** and updates only the **top 4 SSD1306 pages** (32 px) using a direct chunked I2C writer.
- OLED I2C writes are split into small chunks (`OLED_I2C_STREAM_CHUNK_BYTES`) and `tud_task()` / `usb_audio_task()` are serviced between chunks.

Files:

- `core/src/dev/oled.c` (streaming status + partial page update)
- `core/src/config.h` (`OLED_I2C_STREAM_CHUNK_BYTES`)

Tuning knobs:

- `OLED_REFRESH_FPS_STREAMING` (lower = fewer OLED updates while streaming)
- `OLED_I2C_STREAM_CHUNK_BYTES` (lower = shorter blocking I2C transactions; more USB-friendly)
