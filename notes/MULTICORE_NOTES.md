# Multicore Architecture Notes

This document describes the strict multicore architecture implemented in this firmware. The design uses **Pattern B (strict ownership)**: core1 exclusively owns the Heavy context; core0 communicates via event queues.

## Architecture Overview

- **Core1 (audio core)**: Runs Heavy DSP (`hv_process*()`), owns `HeavyContextInterface *`, handles audio buffer production. No blocking I/O, no USB tasks, no `printf()`.
- **Core0 (I/O core)**: Handles initialization, USB/MIDI, peripherals (WS2812), drains control queues. Never calls Heavy APIs directly.

## Communication Queues

Two queues enable inter-core communication (see `core/src/multicore_audio.h`):

1. **ctrl_queue (core0 → core1)**: MIDI and control events
   - core0 pushes via `ctrl_push_notein()`, `ctrl_push_ctlin()`, etc.
   - core1 drains at start of each audio buffer via `multicore_drain_ctrl()`
   - Overflow policy: drop newest

2. **led_queue (core1 → core0)**: LED commands from Pd send hooks
   - Send hooks on core1 parse messages and enqueue `led_cmd_t`
   - core0 drains in main loop via `multicore_drain_led()` and calls `ws2812_set_*()`
   - Overflow policy: drop newest

## Key Design Decisions

### Why strict ownership?

- Maximum isolation: no cross-core contention inside Heavy internals
- Predictable scheduling: control events applied at deterministic points (block boundaries)
- Clear ownership: only one core touches Heavy APIs, easier to reason about thread-safety
- Scales well: adding more subsystems (I2C, displays) doesn't risk audio stability

### Why queues instead of Heavy's thread-safe APIs?

While Heavy provides thread-safe `hv_sendMessageToReceiver*()` APIs, using our own queues:
- Eliminates any cross-core locks inside Heavy
- Gives explicit control over overflow policy and event timing
- Makes the architecture more predictable as the system grows

## Implementation Details

### Heavy Context Ownership

- Heavy context (`HeavyContextInterface *`) is created on core0 during initialization
- Ownership transfers to core1 when `multicore_launch_core1()` is called
- After launch, only core1 may call:
  - `hv_processInlineInterleaved()`
  - `hv_sendMessageToReceiver*()` (via `multicore_drain_ctrl()`)
  - `hv_patch_free()` (on shutdown)

### Send Hook Pattern

Pd send hooks run synchronously during `hv_process*()` on core1. To keep them real-time safe:

1. Parse message and extract minimal data
2. Enqueue command to `led_queue` (non-blocking)
3. Return immediately

Hardware side effects (LED updates, I2C, USB) are deferred to core0, which drains `led_queue` in its main loop.

### Audio Buffer Management

- DMA/IRQ handlers run on core0
- Audio buffer pool uses spinlocks (designed for cross-core producer/consumer)
- core1 produces buffers; DMA consumes them
- Buffer pool initialized on core0 before launching core1

## Queue Overflow Handling

When queues are full, new events are dropped (drop newest policy). This is expected behavior:

- **ctrl_queue overflow**: MIDI events may be lost during bursts. For continuous controls (CC), consider coalescing on core0 before pushing.
- **led_queue overflow**: LED updates may be skipped. Acceptable for visual feedback; audio continues unaffected.

The `ctrl_push_*` functions return `false` on overflow, allowing core0 to implement rate limiting or coalescing if needed.

## Validation

Multicore architecture should be validated under worst-case load:

1. **Stress testing**: Run worst-case USB/MIDI traffic while audio plays; confirm no underruns
2. **Queue backpressure**: Intentionally burst control events; verify overflow policy behaves correctly
3. **Long-run stability**: Run for extended periods to catch race conditions or buffer starvation

**Success criteria**:
- No buffer underruns or audible glitches under stress
- Consistent audio loop timing with safety margin (~1.33 ms at 48 kHz, 64 samples/channel)
- Control event latency within tolerance (typically 1 audio block)

## Reference

For complete architectural rules, failure modes, and validation guidance, see the **Multicore** section in `TECH.md`.
