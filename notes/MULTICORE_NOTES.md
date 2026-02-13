# Multicore Architecture Notes (Current)

Last updated: 2026-02-09

This document reflects the current implementation in `core/src`.

## 1) Core Ownership

- Core1 (audio core): owns `HeavyContextInterface *`, runs Heavy DSP, drains control events before each audio block.
- Core0 (I/O core): initializes system, runs USB/peripherals, applies LED state to hardware.
- Rule: core0 never calls Heavy APIs directly after audio starts.

## 2) Cross-Core Transport (`crosscore_bus`)

Transport lives in `core/src/crosscore_bus.h` and `core/src/crosscore_bus.c`.

### 2.1 Control path (core0 -> core1)

- Mechanism: `ctrl_queue` (FIFO queue).
- Producer API:
  - `crosscore_bus_ctrl_try_push_f(...)`
  - `crosscore_bus_ctrl_try_push_ff(...)`
  - `crosscore_bus_ctrl_try_push_fff(...)`
- Consumer API:
  - `crosscore_bus_ctrl_drain_to_heavy(...)`
- Overflow policy: drop newest (`queue_try_add` returns `false`).

### 2.2 LED path (core1 -> core0)

- Mechanism: latest-wins mailbox (not a queue).
- Producer API:
  - `crosscore_bus_led_publish_color(uint32_t rgb)`
- Consumer API:
  - `crosscore_bus_led_try_consume_latest(crosscore_led_update_t *out)`
- Semantics: if many updates arrive quickly, only the newest committed color is consumed.

## 3) Patch -> Firmware LED Contract

- Send hook owner: only `patch_api_init()` calls `hv_setSendHook()`.
- Supported LED send command:
  - `set_led_color (r g b)`
- `set_led_index` is not supported anymore.

## 4) Firmware LED Apply Path

- Producer side (core1): `patch_api.c` parses `set_led_color` and publishes mailbox color.
- Consumer side (core0): `main.c` (`service_led_from_bus`) consumes latest color and applies `ws2812_set_all(rgb)`.
- Driver scope: `ws2812.c` has no dependency on cross-core transport.

## 5) Backpressure and Observability

- Control path counters are tracked in `patch_api_transport_stats_t`.
- LED path tracks mailbox publish counters (`led_mailbox_publish*`).
- LED mailbox has no queue overflow/drop counter by design.

## 6) Validation Checklist

- Build: `cmake --build build/midi_synth_example/firmware-build -j 8`
- Runtime checks:
  - Audio remains stable under USB/MIDI load.
  - LED follows latest patch color without backlog behavior.
  - No transport calls from `ws2812.c`.
