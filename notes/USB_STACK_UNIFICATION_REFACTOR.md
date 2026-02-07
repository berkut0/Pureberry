# USB Stack Unification Refactor (TinyUSB everywhere)
Last updated: 2026-02-07

This note is a **map for a serious refactor** of the USB subsystem.
The goal is to make USB behavior consistent, debuggable, and scalable (CDC today, CDC+MIDI now, CDC+commands/patch I/O later),
and to eliminate the current “two different USB stacks depending on a flag” situation.

---

## 0) TL;DR (what to do)

1) **Stop using `pico_stdio_usb` entirely.** Always use **TinyUSB device stack** (`tinyusb_device`, `tinyusb_board`) in all builds.
2) Make `--usb-midi` toggle only **interfaces/descriptors + tasks** (CDC-only vs CDC+MIDI), not the whole USB implementation.
3) **Single source of truth** for TinyUSB config (`tusb_config.h`), descriptors, and `tud_task()` servicing.
4) Fix CDC “silence” by providing an explicit **CDC RX/TX task** (at least echo/handshake) and by removing risky `printf` from TinyUSB callbacks.

Recommended initial target for Windows stability: **USB 2.0 (`bcdUSB=0x0200`)**, composite via **IAD**, no vendor reset interface/MS OS 2.0 until CDC-only is rock-solid.

---

## 1) Symptoms (current behavior)

Observed / documented symptoms:

- **Default build (no `--usb-midi`)**
  - Should be “CDC-only console”.
  - On some Windows setups enumerates as a broken device:
    - Device Manager shows a composite device error: **“This device cannot start. (Code 10)”**.
    - **No COM port** appears.
  - Documented in: `docs/USB_DEBUG.md` and `README.md` (Windows USB note).

- **USB MIDI build (`--usb-midi`, `ENABLE_USB_MIDI=ON`)**
  - Device enumerates as MIDI (works).
  - A COM port may appear but **seems silent** even when sending data.
  - Build warnings: `CFG_TUSB_OS redefined` (multiple times).

Important: these two builds are **not the same device** today (different descriptors, different init path, different stack).
So debugging “why A works but B doesn’t” is artificially hard.

---

## 2) What exists today (the root structural problem)

### 2.1 Two USB stacks depending on `ENABLE_USB_MIDI`

Switch is in `core/CMakeLists.txt`:

- `ENABLE_USB_MIDI=OFF` → `pico_enable_stdio_usb(...)` → **`pico_stdio_usb`** owns TinyUSB
- `ENABLE_USB_MIDI=ON` → links `tinyusb_device` + custom descriptors + custom CDC/MIDI → **manual TinyUSB ownership**

This means:
- different descriptors, VID/PID, bcdUSB behavior,
- different tasking model (`pico_stdio_usb` uses background IRQ/timer; custom build uses `tud_task()` in main loop),
- different configuration sources, and even different compile-time macro sets.

### 2.2 TinyUSB config duplication / ambiguity

There are **two** copies of `tusb_config.h`:
- `core/src/tusb_config.h`
- `core/src/usb/tusb_config.h`

They are currently identical, but this is a long-term foot-gun:
include path order decides which one TinyUSB sees. The refactor must leave **exactly one** canonical `tusb_config.h`.

### 2.3 `CFG_TUSB_OS redefined` warning cause

Pico SDK’s TinyUSB integration (rp2040 family) defines:
- `CFG_TUSB_OS=OPT_OS_PICO` via CMake (`sdk/pico-sdk/lib/tinyusb/hw/bsp/rp2040/family.cmake`).

The project’s USB MIDI path additionally defines:
- `CFG_TUSB_OS=OPT_OS_NONE` in `core/CMakeLists.txt`.

Result: macro redefinition warnings.

Even if this “seems harmless”, it is a signal that the build has **multiple sources of truth** for TinyUSB configuration.

### 2.4 Why CDC “silence” is easy to misdiagnose

Current USB MIDI build’s CDC behavior is consistent with “COM exists, but no output / no response” because:

- There is **no CDC command protocol** and no echo task:
  - `usb_cdc_read_chars()` exists, but nothing in `main.c` calls it.
- Logs print very early (init time). If the host opens the port later, there’s no “handshake” output.
- CDC output is gated by `tud_cdc_connected()` (DTR semantics). It’s easy to have a COM device present but not “connected” in TinyUSB’s sense.
- Risk factor: TinyUSB callbacks in `usb_midi.c` call `printf()`. If the stdio path calls into USB servicing (directly or indirectly),
  this can create re-entrancy/ordering hazards.

---

## 3) Likely root cause behind Windows Code 10 (default build)

This is not proven yet, but the highest-probability cause is:

- `pico_stdio_usb` by default enables a **vendor reset interface** + Microsoft OS 2.0 descriptors support (for picotool reset).
- That path can produce device descriptors that are more complex than “plain CDC”.
- Some Windows environments appear to be sensitive to that (or to stale driver cache state when descriptors change).

Regardless of the exact Windows failure mode, the **architectural fix** is still correct:
remove `pico_stdio_usb` from the firmware and own the full device stack ourselves, with a stable descriptor set.

---

## 4) Strategy (what “good” looks like)

### 4.1 One USB “bus” layer, many consumers (same idea as `i2c_bus`)

Treat USB as shared infrastructure on **core0**:
- one init,
- one `tud_task()` servicing loop,
- explicit sub-tasks:
  - `usb_cdc_task()` (TX flush + RX parse),
  - `usb_midi_task()` (already exists),
  - later: `usb_cmd_task()` / `usb_patch_io_task()`.

Then all “features” consume the bus through a narrow API (and never try to init/config USB on their own).

### 4.2 Keep the descriptor surface intentionally small at first

To minimize Windows issues during refactor:
- Use **USB 2.0 (`bcdUSB=0x0200`)**
- Use composite via **IAD** (device class MISC / IAD)
- Enable only:
  - CDC (always)
  - MIDI (only when `ENABLE_USB_MIDI=ON`)
- Do **not** add vendor reset / MS OS 2.0 descriptors in the first pass.
  (If needed later, add it as a separate, well-tested step.)

### 4.3 Remove `printf()` from TinyUSB callbacks

Callbacks run “inside” the USB stack execution context. Logging there risks:
- recursion (USB write calling USB service),
- timing issues,
- deadlocks if future mutexes appear.

Instead:
- callbacks only set flags/counters (or enqueue a lightweight event),
- the main loop prints those events from normal code.

---

## 5) Implementation plan (high-signal steps)

### Phase 1 — Build system: always TinyUSB

P1.1 In `core/CMakeLists.txt`:
   - Remove the `else()` branch that enables `pico_stdio_usb`.
   - Always link:
     - `tinyusb_device`
     - `tinyusb_board`
   - Always compile:
     - `src/usb/usb_descriptors.c`
     - `src/usb/usb_cdc.c`
   - Compile `src/usb/usb_midi.c` only if `ENABLE_USB_MIDI=ON`.

P1.2 Remove project-defined `CFG_TUSB_OS=...` from CMake.
   - Let Pico SDK supply the correct default (`OPT_OS_PICO`).
   - Keep `#ifndef CFG_TUSB_OS` in `tusb_config.h` only as a fallback (or set the same default).

P1.3 Make class enables match the build flag in `core/src/tusb_config.h`:
   - `CFG_TUD_CDC` should remain **always 1** (CDC exists in all builds).
   - `CFG_TUD_MIDI` must follow `ENABLE_USB_MIDI`:
     - `ENABLE_USB_MIDI=ON` → `CFG_TUD_MIDI=1`
     - `ENABLE_USB_MIDI=OFF` → `CFG_TUD_MIDI=0`
   - Keep the policy consistent across:
     - which sources are compiled (`usb_midi.c` on/off),
     - which descriptors are returned (CDC-only vs CDC+MIDI).

Suggested implementation sketch:

```c
// core/src/tusb_config.h
#define CFG_TUD_CDC 1

#ifdef ENABLE_USB_MIDI
#define CFG_TUD_MIDI 1
#else
#define CFG_TUD_MIDI 0
#endif
```

P1.4 Canonicalize `tusb_config.h`:
   - Keep **one** file: recommend `core/src/tusb_config.h` (because `core/src` is already in include dirs).
   - Delete (or rename) `core/src/usb/tusb_config.h` to avoid ambiguous includes.

Deliverable for Phase 1:
- Both builds compile (CDC-only and CDC+MIDI) with the **same TinyUSB base** and without macro redefinition warnings.

### Phase 2 — Descriptors: CDC-only vs CDC+MIDI from the same file

P2.1 Update `core/src/usb/usb_descriptors.c` to support two configurations cleanly:
   - `CDC-only` (interface count = 2, total length = `TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN`)
   - `CDC+MIDI` (interface count = 4, total length = add `TUD_MIDI_DESC_LEN`)

Implementation options:
- **Compile-time**: use `#if defined(ENABLE_USB_MIDI)` to select which descriptor array is compiled/returned.
- or use `CFG_TUD_MIDI` as the selector, but ensure it is consistent and does not require including a different `tusb_config.h`.

Suggested implementation sketch:

```c
// core/src/usb/usb_descriptors.c
#if CFG_TUD_MIDI
#  define ITF_NUM_TOTAL    4
#  define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)
static uint8_t const desc_fs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(/*itf*/0, /*str*/4, 0x81, 8, 0x02, 0x82, 64),
  TUD_MIDI_DESCRIPTOR(/*itf*/2, /*str*/5, 0x03, 0x83, 64),
};
#else
#  define ITF_NUM_TOTAL    2
#  define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
static uint8_t const desc_fs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(/*itf*/0, /*str*/4, 0x81, 8, 0x02, 0x82, 64),
};
#endif
```

Keep:
- `bcdUSB = 0x0200`
- `bDeviceClass/SubClass/Protocol = MISC/Common/IAD`

P2.2 Decide the VID/PID scheme:
   - Current custom descriptors use `VID=0xCAFE` and PID auto-mapped from enabled interfaces.
   - This is reasonable for development because switching MIDI on/off results in a new PID, reducing Windows cache confusion.
   - Keep this scheme in the unified implementation unless you have a strong reason to change it.

Deliverable for Phase 2:
- Windows should enumerate CDC-only mode without Code 10 (expected; if it still fails, the bug is now within *our* descriptors/stack, not in pico_stdio_usb).

### Phase 3 — Main loop: single servicing path

P3.1 Refactor `core/src/main.c` USB init so it is no longer split by `ENABLE_USB_MIDI`:
   - Always call `tusb_init()` on core0.
   - Always call `usb_cdc_init()` (stdio driver registration should happen deterministically).
   - If `ENABLE_USB_MIDI`, call `usb_midi_init()`.

P3.2 In the main loop on core0:
   - Always run `tud_task()` (not only when MIDI is enabled).
   - Always run `usb_cdc_task()` (new) to:
     - drain RX (at least echo for debugging),
     - flush TX if you implement buffering.
   - If `ENABLE_USB_MIDI`, run `usb_midi_task()` (existing).

Deliverable for Phase 3:
- CDC-only build has the same “USB servicing rhythm” as CDC+MIDI build.

### Phase 4 — CDC robustness: prove it’s alive (echo + no re-entrancy)

P4.1 Add minimal CDC RX behavior for deterministic debugging:
   - Echo back received bytes, or implement a tiny command set:
     - `help`
     - `ping`
     - `ver` (build info)
   - This turns “COM port is silent” into a binary fact: either the device is receiving and responding or not.

Suggested implementation sketch:

```c
// core/src/usb/usb_cdc.c
void usb_cdc_task(void) {
  if (!tud_cdc_connected()) return;
  while (tud_cdc_available()) {
    uint8_t buf[64];
    uint32_t n = tud_cdc_read(buf, sizeof(buf));
    if (n) {
      tud_cdc_write(buf, n); // echo (debug)
      tud_cdc_write_flush();
    }
  }
}
```

P4.2 Remove `printf()` from TinyUSB callbacks (at least from `usb_midi.c` mount/unmount/rx callbacks).
   - Replace with a flag like `usb_midi_mounted = true/false` and print from the main loop.

P4.3 Make the stdio/CDC write path non-reentrant:
   - Do **not** call `tud_task()` inside the `printf` hot path.
   - Prefer:
     - a small ring buffer / drop-new policy for stdout,
     - flush from `usb_cdc_task()` in the main loop.

Deliverable for Phase 4:
- COM port not only enumerates, but behaves predictably under “open terminal late / early logs” conditions.

### Phase 5 — Docs + cleanup

P5.1 Update documentation so it matches reality:
   - `README.md`: “USB is TinyUSB always; `--usb-midi` toggles MIDI interface only”.
   - `docs/USB_DEBUG.md`: update root cause and troubleshooting steps; ideally remove mention of `pico_stdio_usb` being the default.
   - `docs/MIDI.md` and `docs/USB_MIDI_README.md`: ensure they reflect new build behavior and where descriptors live.

P5.2 Delete dead/commented code paths that refer to `pico_stdio_usb` assumptions.

---

## 6) Validation checklist (what colleague should verify)

### Build matrix
- `ENABLE_USB_MIDI=OFF` (default): should enumerate as **CDC-only** device; no Code 10.
- `ENABLE_USB_MIDI=ON` (`--usb-midi`): should enumerate as **CDC+MIDI composite**; MIDI works; CDC responds to echo/ping.

### What to check on Windows
- Device Manager:
  - CDC-only: COM port appears under “Ports (COM & LPT)”.
  - CDC+MIDI: COM port + MIDI device.
- If Windows has stale cache from previous VID/PID combos:
  - remove old instances (Device Manager “show hidden devices” / USBDeview),
  - replug.

### Runtime sanity
- `tud_task()` is called continuously (core0 loop).
- No `printf()` in TinyUSB callbacks.
- CDC echo works and does not break MIDI.

---

## 7) Gotchas / pitfalls (things that commonly waste hours)

1) **Two `tusb_config.h` files**: make it one. Otherwise you can “fix” one and still build against the other.
2) **Macro conflicts** (`CFG_TUSB_OS`, `CFG_TUSB_MCU`): don’t fight Pico SDK defaults unless you must; avoid redefining.
   - Note: Pico SDK’s TinyUSB port for RP2350 still goes through the `rp2040` TinyUSB family layer, so `CFG_TUSB_MCU=OPT_MCU_RP2040` is expected.
3) **Windows driver caching**: switching interface sets under the same VID/PID can leave broken instances. Using different PIDs per config helps.
4) **DTR semantics**: CDC output may be intentionally gated until a terminal opens the port. Echo/handshake makes this obvious.
5) **Re-entrancy**: avoid calling USB stack servicing from inside callbacks or inside `printf` path.
6) **Service frequency**: long blocking I2C/SPI work on core0 can starve `tud_task()`. Keep USB servicing early in the main loop and avoid long critical sections.

---

## 8) File map (where to look/edit)

Project code (to refactor):
- `core/CMakeLists.txt` — current stack switch; should become “TinyUSB always”.
- `core/src/main.c` — USB init + main loop tasking.
- `core/src/tusb_config.h` — **canonical** TinyUSB config (keep only one).
- `core/src/usb/usb_descriptors.c` — must support CDC-only and CDC+MIDI cleanly.
- `core/src/usb/usb_cdc.c`, `core/src/usb/usb_cdc.h` — CDC stdio + CDC RX/TX task.
- `core/src/usb/usb_midi.c`, `core/src/usb/usb_midi.h` — MIDI task + callbacks (remove `printf` from callbacks).

SDK references (for understanding what we’re removing/avoiding):
- `sdk/pico-sdk/src/rp2_common/pico_stdio_usb/*` — old path; has vendor reset/MS OS 2.0 complexity.
- `sdk/pico-sdk/src/rp2_common/tinyusb/CMakeLists.txt` and `sdk/pico-sdk/lib/tinyusb/hw/bsp/rp2040/*` — how Pico SDK wires TinyUSB (defaults like `CFG_TUSB_OS=OPT_OS_PICO`).
