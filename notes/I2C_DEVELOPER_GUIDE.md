# I2C Developer Guide

## 1. Purpose and Scope

This document explains how I2C works in this firmware, how to use the I2C
stack correctly, and how to add new I2C devices without breaking architecture
boundaries.

It is intended for:
- developers integrating new devices,
- maintainers reviewing I2C-related changes,
- anyone debugging mixed-runtime traffic on shared I2C buses (for example OLED
  plus touch controller).

This guide is implementation-specific for this repository and tracks current
code behavior.

## 2. Design Goals

The I2C stack is intentionally layered and minimal.

Primary goals:
- one transport boundary for all runtime I2C access,
- deterministic behavior under mixed load on core0,
- no device-layer dependency on DMA internals,
- explicit error semantics and recovery behavior,
- no heap allocation in the hot I2C path.

Non-goals for the current iteration:
- reentrancy redesign of the transport layer,
- generic observability/telemetry framework,
- compatibility with removed `ENABLE_I2C_DMA` / `--i2c-dma` workflows.

## 3. Architecture and Layering

### 3.1 Layer Overview

The stack has three layers:
- `drv/i2c_bus.*`: transport API, arbitration, sync/async transfer path.
- `drv/i2c_reg_io.*`: register-oriented helpers built on `i2c_bus`.
- `dev/*`: device-specific logic and scheduling policy.

### 3.2 Hard Layering Rules

Mandatory rules:
- `dev/*` must use only `i2c_bus` and `i2c_reg_io` for runtime I2C.
- `dev/*` must not include `drv/i2c_dma.h`.
- `dev/*` must not call Pico low-level `i2c_*_timeout_us` directly for runtime
  traffic.
- `dev/*` must not manipulate I2C peripheral registers.

### 3.3 Internal Backend (`i2c_dma`)

`drv/i2c_dma.*` is an internal transport backend:
- consumed by `drv/i2c_bus.c`,
- not part of device-layer API contract,
- implements DMA-fed `IC_DATA_CMD` transaction execution.

Internal status:
- single-inflight execution model (no public queue API),
- completion driven by `i2c_bus_poll()` context.

## 4. Configuration Model

Configuration defaults are in `core/src/config.h` and can be overridden via
`config_local.h` or build defines.

### 4.1 Bus Configuration

Main bus macros:
- `I2C_BUS0_INSTANCE`, `I2C_BUS0_SDA_PIN`, `I2C_BUS0_SCL_PIN`,
  `I2C_BUS0_BAUD`, `I2C_BUS0_TIMEOUT_US`
- `I2C_BUS1_*` mirrors bus0 defaults unless overridden.

Legacy aliases map to bus0:
- `I2C_BUS_INSTANCE`, `I2C_BUS_SDA_PIN`, `I2C_BUS_SCL_PIN`,
  `I2C_BUS_BAUD`, `I2C_BUS_TIMEOUT_US`

### 4.2 DMA-Idle Wait Timeout for Blocking Path

`I2C_BUS_DMA_IDLE_TIMEOUT_US` defines minimum wait while blocking operations
wait for async transport to become idle.

Why this exists:
- full-frame OLED runtime writes are long relative to short register transfers,
- blocking register access must not collide with active async transport.

### 4.3 Device Mapping Macros

Key device mapping macros:
- `OLED_I2C_BUS_ID`, `OLED_I2C_ADDR`, `OLED_WIDTH`, `OLED_HEIGHT`
- `MPR121_I2C_BUS_ID`, `MPR121_I2C_ADDR`, `MPR121_POLL_MS`

By default OLED and MPR121 can share bus 0.

## 5. Runtime Contract and Concurrency

`i2c_bus` is non-reentrant:
- call from one main execution context,
- do not call from ISR,
- async completion is advanced by `i2c_bus_poll()` in that same context.

Implications:
- ISR handlers should set flags only; I2C work runs in task/main code,
- do not call `i2c_bus_*` concurrently from multiple contexts without external
  serialization,
- callback code must stay small and non-blocking.

## 6. Public API Overview

### 6.1 Status Type

Transport result type:
- `i2c_bus_result_t`:
  - `I2C_BUS_RESULT_OK`
  - `I2C_BUS_RESULT_EINVAL`
  - `I2C_BUS_RESULT_ENOT_INIT`
  - `I2C_BUS_RESULT_EBUSY`
  - `I2C_BUS_RESULT_ETIMEOUT`
  - `I2C_BUS_RESULT_EIO`

### 6.2 Synchronous API

Public synchronous calls:
- `i2c_bus_init_once`
- `i2c_bus_get_baud_hz`
- `i2c_bus_write`
- `i2c_bus_read`
- `i2c_bus_write_read`
- `i2c_bus_recover`
- `i2c_bus_poll`

Notes:
- sync calls are status-oriented and validate address/buffer/length,
- sync path waits for async transport idle when required.

### 6.3 Asynchronous API

Public asynchronous calls:
- `i2c_bus_write_async`
- `i2c_bus_read_async`
- `i2c_bus_write_read_async`

Completion callback type:
- `i2c_bus_done_cb_t`

### 6.4 Async Submit Semantics

Submit returns immediately:
- `OK`: accepted for async execution.
- `EINVAL`, `ENOT_INIT`, `EBUSY`, `EIO`: immediate reject.

Rules:
- reject path does not call callback,
- accepted request gets exactly one callback.

### 6.5 Async Completion Semantics

For accepted requests:
- callback runs from `i2c_bus_poll()` context, never from ISR,
- callback carries final result (`OK`, `ETIMEOUT`, `EIO`, etc.),
- callback ordering per bus follows accepted submit sequence under single-inflight
  model.

### 6.6 Lifetime Rules

For async submit:
- `tx`/`rx` buffers must remain valid until callback,
- `user` pointer must remain valid until callback,
- callback must not be `NULL`.

### 6.7 Timeout Semantics

Async `timeout_us`:
- `timeout_us > 0`: per-request deadline,
- `timeout_us == 0`: bus default timeout policy.

## 7. `i2c_reg_io` API and Usage

`i2c_reg_io` provides register operations on top of `i2c_bus`:
- `i2c_reg_write_u8`
- `i2c_reg_read_u8`
- `i2c_reg_read_u16_le`
- `i2c_reg_write_seq`
- `i2c_reg_update_bits`

Error type:
- `i2c_reg_io_result_t` aliases `i2c_bus_result_t`.

Defined aliases include:
- `I2C_REG_IO_OK`
- `I2C_REG_IO_EINVAL`
- `I2C_REG_IO_ENOT_INIT`
- `I2C_REG_IO_EBUSY`
- `I2C_REG_IO_ETIMEOUT`
- `I2C_REG_IO_EIO`

`i2c_reg_write_seq` constraint:
- bounded by `I2C_REG_IO_MAX_STACK_TX` (stack buffer contract).

## 8. Addressing and Transfer Semantics

Addressing:
- public APIs use 7-bit address (`addr7`),
- convert from 8-bit shifted addresses explicitly when integrating third-party
  libraries.

Transfer semantics:
- sync `write/read/write_read` map to transport status and enforce argument checks,
- async write/read/write_read build command stream and execute via internal DMA
  backend.

No hidden downgrade:
- async path does not silently execute blocking fallback.

## 9. Internal Execution Model

This section is for maintainers; it describes behavior exposed via public
contract but does not authorize direct `i2c_dma` use from devices.

### 9.1 Blocking Path (`i2c_bus`)

Before sync transfer:
1. validate bus, init state, address, args,
2. wait for async backend idle with timeout guard,
3. execute blocking I2C transaction with configured timeout.

Purpose:
- prevent blocking and async path collision on one hardware bus instance.

### 9.2 Async Path (`i2c_bus`)

For async request:
1. validate bus/init/address/args/callback,
2. build command words in per-bus static command buffer,
3. submit single transaction to internal backend.

Internal guard:
- one inflight async request per bus.

### 9.3 Backend Path (`i2c_dma`)

Internal backend uses DMA writes to `IC_DATA_CMD`.

Key behavior:
- DMA-complete means FIFO feed complete, not guaranteed STOP on wire,
- state machine waits for STOP detection or abort,
- timeout/abort path resets controller to known state,
- callbacks are invoked from poll context after state cleanup.

## 10. Device Integration Patterns

### 10.1 Register-Oriented Device Pattern

Recommended for devices like MPR121:
1. `i2c_bus_init_once`
2. optional `i2c_bus_recover` on startup if robustness required,
3. configure chip using `i2c_reg_io`,
4. runtime task uses register reads/writes with explicit status handling.

### 10.2 Frame/Stream Device Pattern

Recommended for devices like OLED runtime refresh:
1. build runtime payload in device buffer,
2. submit async transfer via `i2c_bus_*_async`,
3. keep device-level inflight/error/retry state machine explicit.

### 10.3 Mixed Bus Pattern (OLED + MPR121)

When two devices share one bus:
- keep OLED runtime path async,
- keep touch path status-driven and bounded on recover,
- treat `EBUSY` as backpressure (defer),
- use recover path only for transport failure classes.

## 11. Error Handling Strategy

### 11.1 Result Mapping Strategy

Use this meaning consistently:
- `EINVAL`: programming/config misuse.
- `ENOT_INIT`: bus or module init missing.
- `EBUSY`: temporary contention/backpressure.
- `ETIMEOUT`: transfer deadline hit.
- `EIO`: low-level transfer/abort error.

### 11.2 Device Policy Guidance

Recommended behavior:
- `EBUSY`: defer/retry later, avoid aggressive recover.
- `ETIMEOUT`/`EIO`: bounded recover/re-init path.
- repeated failures: controlled reset of device transport state.

### 11.3 Recovery Guidance

`i2c_bus_recover` is acceptable in task/main context when justified.

Rules:
- do not call from ISR,
- keep recovery bounded and explicit,
- avoid infinite recover loops.

## 12. Polling and Scheduling Requirements

`i2c_bus_poll()` must run frequently from main loop to advance async transport.

Scheduling advice:
- avoid long blocking sections in same context,
- keep callbacks short,
- avoid heavy work inside callback; defer to task state machine.

`main.c` currently calls `i2c_bus_poll()` in normal peripheral service loop.

## 13. Performance and Memory Notes

### 13.1 Runtime Performance

General behavior:
- async OLED updates reduce long blocking sections on core0,
- register-device operations remain simple and predictable.

### 13.2 Static Buffer Policy

`I2C_BUS_ASYNC_CMD_BUF_WORDS` is static by design.

Rationale:
- deterministic memory,
- no dynamic allocation in transport hot path,
- known worst-case transfer fit.

Current fit policy:
- compile-time guard ensures one full OLED frame async write fits command buffer.

Memory impact:
- command buffer is per bus state (`uint32_t[I2C_BUS_ASYNC_CMD_BUF_WORDS]`),
- with default `1152` words this is `4608` bytes per bus.

### 13.3 Bus Baud Considerations

High baud may work in practice but can exceed strict datasheet guarantees for
some devices.

Current example:
- MPR121 driver warns if bus baud exceeds 400 kHz guaranteed range.

## 14. Build and Feature Wiring

Build through the required entrypoint:
1. `python scripts/build_firmware.py pd-patches/mpr121_test.pd --clean --mpr121 --oled`
2. `python scripts/build_firmware.py pd-patches/hv_sine_simple_test.pd --clean --oled`
3. `python scripts/build_firmware.py pd-patches/hv_sine_simple_test.pd --clean`

Relevant firmware feature flags:
- `ENABLE_OLED`
- `ENABLE_MPR121`

Important:
- no user-facing `ENABLE_I2C_DMA` or `--i2c-dma` workflow in current architecture.

## 15. Correct Usage Examples

### 15.1 Register Read via `i2c_reg_io`

```c
uint8_t value = 0u;
i2c_reg_io_result_t r = i2c_reg_read_u8(I2C_BUS_ID_0, 0x5Au, 0x00u, &value);
if (r != I2C_REG_IO_OK) {
    // handle transport/device error
}
```

### 15.2 Register Update Bits

```c
i2c_reg_io_result_t r = i2c_reg_update_bits(
    I2C_BUS_ID_0,
    0x5Au,
    0x5Eu,
    0x0Fu,
    0x08u
);
```

### 15.3 Async Write Pattern

```c
static bool inflight = false;

static void dev_done(void *user, i2c_bus_result_t result) {
    (void)user;
    inflight = false;
    // store result in device state machine
}

void dev_task(void) {
    if (inflight) return;
    i2c_bus_result_t r = i2c_bus_write_async(
        I2C_BUS_ID_0,
        0x3Cu,
        tx_buf,
        tx_len,
        20000u,
        dev_done,
        NULL
    );
    if (r == I2C_BUS_RESULT_OK) {
        inflight = true;
    } else if (r == I2C_BUS_RESULT_EBUSY) {
        // defer and retry later
    } else {
        // explicit error path
    }
}
```

## 16. Anti-Patterns (Do Not Do This)

- Do not include `drv/i2c_dma.h` from `dev/*`.
- Do not call `hardware/i2c.h` transfer helpers directly in device runtime code.
- Do not call `i2c_bus_*` from ISR context.
- Do not pass temporary async buffers that can go out of scope before callback.
- Do not hide recover loops or retry forever without bounds.
- Do not assume async callback can execute heavy logic safely.

## 17. Checklist: Adding a New I2C Device

1. Choose pattern:
   - register-oriented: `i2c_reg_io`,
   - frame/stream-oriented: `i2c_bus_*_async` with explicit state machine.
2. Add config macros (address, bus ID, optional IRQ/poll timing).
3. Initialize bus once (`i2c_bus_init_once`).
4. Keep runtime I2C access inside approved transport boundary.
5. Define explicit error policy by result class.
6. For async use, define callback, inflight state, and buffer lifetime ownership.
7. Test with other active devices on same bus.

## 18. Checklist: Reviewing an I2C PR

1. Are layer boundaries preserved (`dev/*` -> `i2c_bus` / `i2c_reg_io`)?
2. Are async callback/lifetime rules respected?
3. Is error handling explicit and bounded?
4. Is scheduling impact considered (`i2c_bus_poll()` cadence, callback weight)?
5. Are new constants/macros justified and documented?
6. Are docs updated to reflect behavior changes?

## 19. Known Limitations and Future Work

Current known constraints:
- transport remains non-reentrant by contract,
- one inflight async transaction per bus in current `i2c_bus` policy,
- no generic instrumentation framework in this iteration.

Possible future improvements (only if justified by measured need):
- explicit non-blocking register helper variants for high-rate sensors,
- bus-load aware pacing policies for mixed OLED + input devices,
- deliberate reentrancy model update with proof/tests.

## 20. Quick Reference

- Use `i2c_bus` for transport operations.
- Use `i2c_reg_io` for register devices.
- Keep `dev/*` independent from DMA internals.
- Drive async progress via `i2c_bus_poll()` in main context.
- Treat `EBUSY` as backpressure, not immediate failure.
- Keep recover logic bounded and explicit.
