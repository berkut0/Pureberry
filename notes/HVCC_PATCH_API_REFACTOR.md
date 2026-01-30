# HVCC Patch API refactor (breaking change)

This note documents the **current** technical state of the HVCC/Pd integration after the refactor, focusing on:

- **Standard hvcc MIDI injection** (so Pd MIDI objects like `[notein]` work)
- A **single owner** of `hv_setSendHook()` (to avoid “last one wins”)
- A clean split between **transport** (queues/drain) and **Patch API semantics**

---

## Why this refactor exists

hvcc is not just a DSP compiler: it defines the contract between a Pd patch and the host (firmware).

### 1) MIDI in hvcc is standardized

hvcc’s Pd MIDI objects (`[notein]`, `[ctlin]`, …) are wrappers that read from internal receivers:

- `[notein]` reads `__hv_notein`
- `[ctlin]` reads `__hv_ctlin`
- etc. (see `third_party/hvcc/docs/04.midi.md` and `third_party/hvcc/hvcc/interpreters/pd2hv/libs/pd/*.pd`)

**Implication:** if firmware sends MIDI to `notein` (custom), then **standard Pd objects do not work** and patch authors end up using `r notein` / custom naming.

### 2) sendHook is the canonical patch → firmware channel

hvcc’s send hook is called for any `[send]`/`[s]` and is the canonical way to export commands/events from a patch to the host.

### 3) `@hv_param` is for scalar parameters (UI-like)

`@hv_param` exposes scalar receivers/senders and is suitable for stable, automatable parameters (`float/bool/int`).
For multi-argument commands, keep **send messages** (sendHook) and document their formats.

---

## Invariants (what is “correct” now)

1) **No backwards compatibility with old custom MIDI receiver names.** Patches and firmware are updated together.
2) hvcc project name is fixed: `hvcc ... -n patch`, so the generated API is `Heavy_patch.h` and `hv_patch_new()` / `hv_patch_free()`.
3) Firmware injects MIDI into **hvcc-standard receivers** `__hv_*`, so Pd MIDI objects work.
4) sendHook handling is **RT-safe**: parse minimally, enqueue, return quickly.
5) There is exactly **one** call site for `hv_setSendHook()`.

---

## Where the contracts live in the codebase (current layout)

### Patch API contract

**`core/src/patch_api.c` / `core/src/patch_api.h`**

- **MIDI injection:** `patch_api_push_*()` sends messages to `__hv_*` receivers with canonical argument order.
- **Single send hook owner:** `patch_api_init(ctx)` calls `hv_setSendHook(ctx, ...)` exactly once.
- **Command table (implemented by hash dispatch):**
  - `set_led_color (r g b)` → `led_queue`
  - `set_led_index (idx r g b)` → `led_queue`
  - Future commands: prefer names like `cmd_*` (names in the *patch*, not in C) and add hash + handler here.

### Transport layer (queues/drain)

**`core/src/multicore_audio.c` / `core/src/multicore_audio.h`**

- `ctrl_queue`: core0 → core1 control events; core1 drains before each audio buffer and calls `hv_sendMessageToReceiver*()`.
- `led_queue`: core1 (sendHook) → core0 LED commands; core0 drains and calls `ws2812_*`.
- Generic push helpers:
  - `ctrl_push_hash_f/ff/fff(receiver_hash, ...)`

This layer does **not** define what receivers exist or the MIDI argument order.

### WS2812 driver

**`core/src/dev/ws2812.c`**

- **Never calls `hv_setSendHook()`** (this prevents “last one wins” as the project grows).
- Consumes LED commands from `led_queue` via `multicore_drain_led()` on core0.

### USB MIDI ingress

**`core/src/usb/usb_midi.c`**

- Parses USB MIDI packets and calls `patch_api_push_*()` (not `ctrl_push_*()` directly).

### Firmware entry

**`core/src/main.c`**

- Calls `hv_patch_new()`
- Immediately calls `patch_api_init(heavy_context)` (single sendHook registration point)
- Launches audio core; main loop drains queues and runs peripherals

---

## MIDI contract (what firmware sends; what patches should use)

Patches should use standard Pd objects:

- `[notein]`, `[ctlin]`, `[bendin]`, `[pgmin]`, `[touchin]`, `[polytouchin]`

Firmware injects into hvcc receivers with canonical argument order (reference: `third_party/hvcc/tests/src/test_midi.cpp`):

| Pd object | hvcc receiver | Message (order) |
|---|---|---|
| `[notein]` | `__hv_notein` | (pitch, velocity, channel0) |
| `[ctlin]` | `__hv_ctlin` | (value, cc, channel0) |
| `[bendin]` | `__hv_bendin` | (bend14, channel0) |
| `[pgmin]` | `__hv_pgmin` | (program, channel0) |
| `[touchin]` | `__hv_touchin` | (pressure, channel0) |
| `[polytouchin]` | `__hv_polytouchin` | (pressure, note, channel0) |

`channel0` is 0..15.

---

## sendHook contract (patch → firmware commands)

- sendHook runs inside the Heavy/audio context (RT). Do not do I/O or slow work there.
- The hook parses by `sendHash` and enqueues to one or more queues.
- Core0 drains queues in the main loop and performs the actual work (GPIO/I2C/display/etc).

### LED commands (current official formats)

- `set_led_color (r g b)` — three floats (0–1 or 0–255), sets all LEDs
- `set_led_index (idx r g b)` — index + three floats, sets a single LED

---

## `@hv_param` guidance (future-proofing)

Use `@hv_param` for **scalar state** inputs that are UI-like. Potentiometers use **Daisy-style** names (see `core/src/config.h` for pins; default ADC GPIO 26–29):

- `[r knob1 @hv_param 0 1 0]` … `[r knob4 @hv_param 0 1 0]`

If **POTS_BACKEND** is **NONE** (or **POTS_COUNT** is 0), firmware does not push to `knob*`; those receivers stay silent. For other scalar inputs (e.g. buttons): `[r hw_btn1 @hv_param 0 1 0 bool]`.

For **event/packet** style inputs (encoder delta, button press events, I2C packets), use ordinary receivers/messages unless you explicitly want a UI parameter.

---

## Notes / follow-ups

- Patch examples may still contain legacy `r notein`. Those must be migrated to `[notein]` (and similar) to benefit from this refactor.
- Keep `hv_setSendHook()` ownership centralized in `patch_api_init()`; do not reintroduce hooks in drivers.

