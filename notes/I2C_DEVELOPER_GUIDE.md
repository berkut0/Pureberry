# I2C Developer Guide

## 1. Purpose and Scope

This document explains how I2C works in this firmware, how it is expected to be used, and how to add new I2C devices safely.

It is written for:
- developers integrating new I2C devices,
- maintainers reviewing I2C-related changes,
- anyone debugging I2C bus behavior (especially OLED + other devices on the same bus).

This guide is implementation-specific for this repository.


### 1.1 Operating Modes (DMA Is Optional)

The firmware supports two I2C operating modes:

- Blocking-only mode (`ENABLE_I2C_DMA=OFF`):
  - all runtime traffic uses blocking `i2c_bus_write/read/write_read`,
  - no DMA queue is used.
- Hybrid mode (`ENABLE_I2C_DMA=ON`):
  - blocking API is still used by register-oriented devices,
  - stream/frame devices may use DMA submit via `i2c_bus_dma_submit_writes`,
  - `i2c_bus_poll()` must run regularly to advance DMA completion.

Important:
- DMA is optional by design.
- If DMA init fails (for example no DMA channel available), bus traffic can still work in blocking mode.


## 2. Design Goals

The I2C stack is intentionally layered:

- `drv/i2c_bus.*`: transport layer and bus arbitration.
- `drv/i2c_reg_io.*`: register-oriented helpers built on `i2c_bus`.
- `dev/*`: device-specific logic (MPR121, OLED, etc).

Primary goals:
- one transport boundary for all runtime I2C access,
- no direct low-level I2C/DMA control from device code,
- deterministic behavior when blocking traffic and DMA traffic share one bus,
- clear error semantics.


## 3. Layer Responsibilities

### 3.1 `i2c_bus` (transport + arbitration)

Responsibilities:
- bus init and config access,
- blocking read/write/write+read operations with timeouts,
- optional DMA TX path orchestration,
- arbitration between blocking path and DMA path,
- bus recovery,
- periodic DMA polling entry point.

Non-responsibilities:
- device register maps,
- device business logic,
- device-specific protocol state machines.

### 3.2 `i2c_reg_io` (register helpers)

Responsibilities:
- common register access patterns on top of `i2c_bus`:
  - write 8-bit register,
  - read 8/16-bit registers,
  - write register + payload sequence,
  - read-modify-write bits.

Non-responsibilities:
- DMA internals,
- stream/frame protocols (like full-frame OLED DMA).

### 3.3 `dev/*` (device logic)

Responsibilities:
- protocol/register programming specific to a chip/module,
- scheduling policy for that device (polling interval, IRQ handling, etc),
- translating data to/from app layer.

Rules:
- runtime I2C transactions must go through `i2c_bus` or `i2c_reg_io`.
- do not access hardware I2C/DMA registers directly from device files.


## 4. Configuration Model

All key defaults are in `core/src/config.h` and can be overridden via `config_local.h` or build defines.

### 4.1 Bus selection and electrical config

Main macros:
- `I2C_BUS0_INSTANCE`, `I2C_BUS0_SDA_PIN`, `I2C_BUS0_SCL_PIN`, `I2C_BUS0_BAUD`, `I2C_BUS0_TIMEOUT_US`
- `I2C_BUS1_*` mirrors bus0 by default.

Default bus0 timeout:
- `I2C_BUS0_TIMEOUT_US = 5000`

### 4.2 DMA/blocking coexistence timeout

`I2C_BUS_DMA_IDLE_TIMEOUT_US` (default `30000`) is used as minimum wait when blocking path needs DMA path to become idle.

Why:
- full-frame OLED DMA can keep bus busy longer than short blocking timeout.

### 4.3 Device-to-bus mapping

Key macros:
- `OLED_I2C_BUS_ID` (default `0`)
- `MPR121_I2C_BUS_ID` (default `0`)
- `OLED_I2C_ADDR`, `MPR121_I2C_ADDR`

This means OLED and MPR121 may share the same physical I2C bus by default.


## 5. Runtime Contract and Concurrency

`i2c_bus` contract (from header and implementation):
- non-reentrant,
- call from one main execution context,
- not from ISR,
- `i2c_bus_poll()` advances DMA completion state machine and must be called regularly.

Implications:
- do not call `i2c_bus_*` concurrently from multiple threads/cores without introducing external serialization.
- ISR should set flags only; perform I2C work in task/main context.


## 6. Public API Overview

### 6.1 `i2c_bus` status API

Main status type:
- `i2c_bus_result_t`:
  - `OK`, `EINVAL`, `ENOT_INIT`, `EBUSY`, `ETIMEOUT`, `EIO`

Core calls:
- `i2c_bus_init_once(id)`
- `i2c_bus_get_baud_hz(id)`
- `i2c_bus_write(id, addr7, buf, len, nostop)`
- `i2c_bus_read(id, addr7, buf, len)`
- `i2c_bus_write_read(id, addr7, tx, tx_len, rx, rx_len)`
- `i2c_bus_recover(id)`
- `i2c_bus_poll()`

Compatibility calls (raw Pico-style results) are still available:
- `i2c_bus_write_timeout`, `i2c_bus_read_timeout`, `i2c_bus_write_read_timeout`

Use status API for new device code unless there is a strong reason not to.

### 6.2 DMA API through `i2c_bus` (optional)

Available when `ENABLE_I2C_DMA` is enabled:
- `i2c_bus_dma_init(id, dma_chan, dma_irq_index)`
- `i2c_bus_dma_ready(id)`
- `i2c_bus_dma_busy(id)`
- descriptor type `i2c_bus_dma_write_req_t`
- `i2c_bus_dma_submit_writes(id, addr7, reqs, req_count)`

`i2c_bus_dma_write_req_t` fields:
- `bytes`, `len`
- `restart_first`
- `timeout_us`
- `cmd_buf`, `cmd_buf_words`

Important lifetime rule:
- `cmd_buf` memory must remain valid until DMA transfer completion.
- `bytes` are only needed during submit (they are converted to `cmd_buf` inside `i2c_bus`).

### 6.3 `i2c_reg_io` API

- `i2c_reg_write_u8`
- `i2c_reg_read_u8`
- `i2c_reg_read_u16_le`
- `i2c_reg_write_seq`
- `i2c_reg_update_bits`

Stack buffer limit in `write_seq`:
- max payload currently bounded by `I2C_REG_IO_MAX_STACK_TX` in `i2c_reg_io.c` (default `64`, including register byte).

### 6.4 Addressing and Transfer Semantics

- `i2c_bus` APIs use 7-bit I2C address (`addr7`).
- Some external libraries use shifted 8-bit address (`addr8 = addr7 << 1`), convert explicitly when needed.
- `i2c_bus_write(..., nostop)` controls whether STOP is sent at the end of that blocking write.
- `i2c_bus_dma_submit_writes` always encodes STOP on the last byte of each request.
  - Result: each request is a separate on-wire I2C transaction.
  - A batch is an ordered sequence of transactions, not one monolithic transaction.

### 6.5 API Edge Cases and Init Order

- `i2c_bus_write_read` requires valid non-empty TX and RX buffers (`tx_len > 0`, `rx_len > 0`), otherwise `EINVAL`.
- `i2c_bus_dma_init` requires bus init first (`i2c_bus_init_once`).
- `i2c_bus_dma_busy` returns `false` if DMA is not initialized/ready on that bus.


## 7. How Transactions Work Internally

### 7.1 Blocking path

Before each blocking transfer, `i2c_bus`:
1. validates bus/init/args,
2. waits until DMA path is idle (if DMA enabled on this bus),
3. performs Pico SDK blocking transfer with timeout.

This prevents blocking and DMA traffic from colliding on shared bus hardware.

### 7.2 DMA path

DMA implementation (`i2c_dma`) writes words to `IC_DATA_CMD`.

Notes:
- DMA completion means FIFO feed completed, not necessarily STOP on wire.
- state machine waits for `STOP_DET` (or handles `TX_ABRT`).
- timeout and abort path reset controller and clear interrupt latches.
- stale/late IRQ handling is explicitly guarded.

### 7.3 Batched DMA submit (`submit_writes`)

`i2c_bus_dma_submit_writes` behavior:
1. validates all requests,
2. requires DMA idle before queuing sequence,
3. builds all command buffers first,
4. then enqueues all requests in order.

What this guarantees:
- no enqueue starts until all requests are validated and command buffers are prepared,
- request order is preserved in the DMA queue,
- suitable for ordered multi-part flows (for example OLED window setup + framebuffer payload).

What this does not guarantee:
- not hardware-atomic across all requests,
- if an unexpected enqueue failure happens after at least one request was submitted, partial sequence is possible,
- each request is still its own I2C transaction (STOP at request end).


## 8. Device Integration Patterns

### 8.1 Register-based devices (recommended path)

Use `i2c_reg_io`.

Typical init flow:
1. `i2c_bus_init_once(bus_id)`
2. optional `i2c_bus_recover(bus_id)` if startup robustness is needed,
3. program register defaults using `i2c_reg_*`,
4. runtime reads/writes via `i2c_reg_*` only.

Example in repo:
- `core/src/dev/mpr121_touch.c`

### Why this pattern

- less boilerplate,
- uniform error handling,
- no transport leakage into device code.

### 8.2 Stream/frame devices

Use `i2c_bus` directly.

Cases:
- blocking stream writes (small or infrequent),
- DMA batched writes with descriptors for high-throughput frame updates.

Example in repo:
- `core/src/dev/oled.c` using `i2c_bus_dma_write_req_t[]` + `i2c_bus_dma_submit_writes`.


## 9. Correct Usage Examples

### 9.1 Simple register read

```c
uint8_t whoami = 0;
i2c_reg_io_result_t r = i2c_reg_read_u8(I2C_BUS_ID_0, 0x5Au, 0x00u, &whoami);
if (r != I2C_REG_IO_OK) {
    // handle error
}
```

### 9.2 Register update bits

```c
i2c_reg_io_result_t r = i2c_reg_update_bits(
    I2C_BUS_ID_0,
    0x5Au,
    0x5Eu,
    0x0Fu,
    0x08u
);
```

### 9.3 DMA sequence submit (stream/frame)

```c
i2c_bus_dma_write_req_t reqs[] = {
    {
        .bytes = header_bytes,
        .len = header_len,
        .restart_first = false,
        .timeout_us = 20000u,
        .cmd_buf = header_cmd_buf,
        .cmd_buf_words = header_cmd_buf_words,
    },
    {
        .bytes = payload_bytes,
        .len = payload_len,
        .restart_first = false,
        .timeout_us = 50000u,
        .cmd_buf = payload_cmd_buf,
        .cmd_buf_words = payload_cmd_buf_words,
    },
};
i2c_bus_result_t r = i2c_bus_dma_submit_writes(I2C_BUS_ID_0, 0x3Cu, reqs, 2u);
```


## 10. Anti-Patterns (Do Not Do This)

- Do not call `hardware/i2c.h` transfer functions directly in device modules for runtime traffic.
- Do not include or manipulate `hardware/regs/i2c.h` in device modules.
- Do not mix direct DMA queue manipulation with bus-level API.
- Do not call `i2c_bus_*` from ISR.
- Do not pass temporary/stack command buffers to DMA if they may go out of scope before completion.
- Do not bypass `i2c_bus_poll()` when DMA is enabled.
- Do not assume `i2c_bus_dma_submit_writes` creates one single on-wire I2C transaction.


## 11. Error Handling Strategy

Recommended mapping:
- `EINVAL`: bad arguments, misuse of API.
- `ENOT_INIT`: bus/DMA path not initialized.
- `EBUSY`: bus or DMA path not available right now.
- `ETIMEOUT`: transfer timeout.
- `EIO`: low-level failure (NACK/abort/other I/O failure).

Guidelines:
- fail fast on init/programming failures,
- for runtime read errors on shared bus, optionally attempt:
  - `i2c_bus_recover()`,
  - reprogram essential device registers.

MPR121 follows this approach in task loop recovery path.


## 12. Polling and Scheduling Requirements

If DMA is used:
- `i2c_bus_poll()` must be called frequently from main context.
- Avoid long blocking sections in the same context that would starve polling.

If DMA is not used:
- polling is still safe to call, but not required for I2C progress.

For mixed devices (example OLED + MPR121):
- non-display device task should avoid I2C operations while DMA is active if timing is sensitive.
- current MPR121 task checks `i2c_bus_dma_busy()` and defers read.


## 13. Performance Notes

- OLED full-frame DMA is optimized for throughput and reduced blocking time.
- Register devices prioritize correctness and simplicity through blocking `i2c_reg_io`.
- High bus baud can work in practice but may exceed some device datasheet guarantees.
  - Current MPR121 code emits warning if bus baud is above 400 kHz.


## 14. Build and Feature Flags

Key build flags:
- `ENABLE_OLED`
- `ENABLE_MPR121`
- `ENABLE_I2C_DMA`

Source wiring:
- `i2c_reg_io.c` is part of core build sources.
- MPR121 is implemented in-tree and no longer depends on external `pico-mpr121`.


## 15. Checklist: Adding a New I2C Device

1. Decide device class:
   - register-oriented -> use `i2c_reg_io`,
   - stream/frame-oriented -> use `i2c_bus` and possibly DMA descriptors.
2. Add config macros (address, bus id, optional timing/IRQ pins).
3. Initialize bus with `i2c_bus_init_once`.
4. Keep all runtime I2C calls inside `i2c_bus` boundary.
5. If DMA is needed:
   - initialize DMA through `i2c_bus_dma_init`,
   - provide persistent command buffers,
   - call `i2c_bus_poll()` regularly.
6. Handle errors with clear policy (retry/recover/fail).
7. Add integration test scenario with other active I2C devices on same bus.


## 16. Checklist: Reviewing an I2C PR

- Are layer boundaries respected (`dev` does not leak into transport internals)?
- Is API usage status-oriented (`i2c_bus_result_t`) where appropriate?
- Are DMA buffer lifetime rules satisfied?
- Is non-reentrant contract respected?
- Are timeouts and recovery behavior explicit and justified?
- If device shares bus with OLED DMA, is contention behavior addressed?
- Are configuration macros documented and sane by default?


## 17. Known Limitations and Future Improvements

Current limitations:
- DMA helper is TX-only.
- `i2c_reg_write_seq` stack-bound payload limit.
- Non-reentrant global contract (single main execution context).

### 17.1 Why Full Non-Blocking I2C Is Deferred

For an audio-oriented device, a fully non-blocking I2C API would likely be beneficial.

Observed behavior in this project:
- I2C activity can introduce audible hiccups in USB-audio scenarios without overclocking.
- Overclocking can reduce or hide the issue in practice, but it does not remove the underlying scheduling/contention pressure from I2C operations.

Why this is deferred for now:
- implementing full non-blocking support for `read/write/write_read` is a significant architecture change,
- it requires request orchestration, completion-driven state machines, and broader driver migration,
- this is currently larger than the immediate project priorities.

Status:
- intentionally parked as a long-term improvement,
- not considered a near-term requirement for ongoing feature development.

Reasonable future improvements:
- optional chunked large register writes in `i2c_reg_io`,
- explicit instrumentation counters for bus/DMA health,
- stricter debug assertions for ISR-context misuse.


## 18. Quick Reference

- Use `i2c_bus` for transport.
- Use `i2c_reg_io` for register devices.
- Keep `dev/*` free from low-level I2C/DMA register manipulation.
- Poll `i2c_bus_poll()` when DMA is enabled.
- Respect DMA buffer lifetime.
