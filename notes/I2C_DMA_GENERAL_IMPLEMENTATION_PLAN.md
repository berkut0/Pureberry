# I2C TX via DMA (RP2040/RP2350) — generalized implementation plan

Date: 2026-01-31

## Purpose

Implement a reusable, non-blocking **I2C transmit (TX) driver backed by DMA** for RP2040/RP2350-class chips (Synopsys DW_apb_i2c), so higher-level device drivers (OLEDs, sensors, expanders, etc.) can:

- queue I2C transactions without CPU busy-wait loops,
- keep time-critical work responsive while I2C bytes are being shifted out,
- share a single implementation across multiple I2C peripherals/devices.

This note describes a generalized approach (not tied to any specific I2C device protocol).

## What the SDK already provides

Pico SDK provides the building blocks but **does not** provide a ready-made “i2c_write_dma()”:

- **DMA primitives**: channel claim/configure, DREQ pacing, IRQ on completion (`hardware/dma.h`).
- **I2C primitives**: init/baud, pins, and blocking helpers (`hardware/i2c.h`).
- **Hardware register access**: `i2c_inst_t*` → `i2c->hw` for DW_apb_i2c registers.
- **DMA request signals (DREQ)**:
  - RP2350: `DREQ_I2C0_TX`, `DREQ_I2C1_TX` exist (paced by I2C TX FIFO availability).
  - RP2040 also has equivalent DREQs (names differ by header set).

Because SDK’s `i2c_write_*` functions are blocking, the DMA solution must drive the I2C controller via registers and implement a small transaction state machine.

## Key design choice: what DMA writes

DW_apb_i2c transmits bytes written to the `DATA_CMD` register. Importantly, `DATA_CMD` includes control bits (e.g., STOP/RESTART) in the same word as the data.

The most robust approach is:

- Build a **command stream** in memory where each element is a `uint16_t` or `uint32_t` value representing one `DATA_CMD` write:
  - lower 8 bits: data byte
  - control bits: STOP on the last byte, RESTART on the first byte when needed
- Configure DMA to write these words to `&i2c->hw->data_cmd` paced by `DREQ_I2C*_TX`.

This gives deterministic STOP generation without CPU intervention and keeps a single code path for any device protocol.

## Constraints and assumptions

- This plan covers **TX**. RX (reads) can be added later (more state and error cases).
- One physical I2C bus can only service one transfer at a time. The implementation should serialize transfers per `i2c_inst_t`.
- Callers must not use Pico SDK blocking `i2c_write_*` on the same `i2c_inst_t` concurrently.
- For multi-core projects: pick a single “I2C owner core” (recommended) and communicate via queues; avoid cross-core register access.

## Proposed module layout

Add a small reusable module, e.g.:

- `core/src/drv/i2c_dma.h`
- `core/src/drv/i2c_dma.c`

Keep device-specific code (OLED, sensors) separate and only depend on `i2c_dma_*` API.

## Public API (minimal but extensible)

Suggested C API:

```c
typedef enum {
  I2C_DMA_OK = 0,
  I2C_DMA_EBUSY,
  I2C_DMA_EINVAL,
  I2C_DMA_EABORT,
  I2C_DMA_ETIMEOUT,
} i2c_dma_result_t;

typedef struct {
  i2c_inst_t *i2c;
  uint8_t addr7;
  const uint16_t *cmds;     // DATA_CMD words
  size_t cmd_count;
  uint32_t deadline_us;     // optional safety timeout
  void (*on_done)(void *user, i2c_dma_result_t result);
  void *user;
} i2c_dma_txn_t;

void i2c_dma_init(i2c_inst_t *i2c, int dma_chan, int dma_irq_index);
bool i2c_dma_submit(const i2c_dma_txn_t *t);
void i2c_dma_poll(void); // optional: advances timeouts, calls callbacks if using poll instead of IRQ
bool i2c_dma_busy(i2c_inst_t *i2c);
```

Notes:

- `cmds` is generic and lets any device build its own protocol (command prefixes, data streams, etc.) without special-casing.
- A higher-level helper can be provided to convert raw bytes into `DATA_CMD` words with STOP on the last byte.

## Building `DATA_CMD` command streams

Provide helpers that standardize correctness and reduce copy/paste:

```c
// Builds a simple write transaction (no RESTART). STOP set on last byte.
size_t i2c_dma_build_write(uint16_t *out, size_t out_cap, const uint8_t *bytes, size_t n);

// Optionally support sequences: [write] + RESTART + [read] later.
// For now, keep the API TX-only and allow callers to chain transactions.
```

Implementation details:

- `STOP` bit: set only on last element.
- `RESTART` bit: set when required (e.g., for combined transactions). For pure writes, typically not needed.

## Transaction engine (state machine)

Implement a small per-I2C state:

- `IDLE`
- `DMA_ACTIVE` (DMA is pushing DATA_CMD words)
- `WAIT_STOP` (DMA finished, but I2C still shifting bytes; wait for STOP_DET)
- `DONE` (callback ready)
- `ERROR` (TX_ABRT_SOURCE or timeout)

Flow:

1. **Prepare controller**
   - Disable/enable controller as required to set `TAR` (target address).
   - Clear pending abort/stop flags.
2. **Start DMA**
   - Configure DMA:
     - write address: `&i2c->hw->data_cmd`
     - read address: `txn->cmds`
     - transfer count: `cmd_count`
     - data size: 16-bit or 32-bit (match how `data_cmd` is defined; use 32-bit if unsure).
     - DREQ: `DREQ_I2C*_TX`
   - Enable the DMA channel.
3. **Completion**
   - On DMA IRQ: transition to `WAIT_STOP`.
   - In `WAIT_STOP`: poll `RAW_INTR_STAT.STOP_DET` (or `clr_stop_det` mechanism) until stop observed.
   - Check abort:
     - if `tx_abrt_source != 0`, treat as error; clear via `clr_tx_abrt`.
4. **Timeout**
   - If deadline exceeded, abort the transaction (disable DMA channel, optionally reset I2C controller) and report `I2C_DMA_ETIMEOUT`.

## Concurrency model: a queue per bus

To make this “general” for many peripherals:

- Maintain a per-bus FIFO queue of `i2c_dma_txn_t` (or pointers).
- `i2c_dma_submit()` enqueues and starts immediately if the bus is idle.
- DMA IRQ handler schedules the next transaction (or sets a flag and lets `i2c_dma_poll()` start it).

This keeps device drivers simple:

- they only construct `cmds[]` and call `submit`,
- they do not care about bus arbitration or “is it busy”.

## Memory/lifetime rules

DMA reads from memory asynchronously, so `cmds` must remain valid until completion.

Two options:

1) Caller-owned buffer (simplest contract, zero-copy)
- Device driver keeps a static buffer or a long-lived pool.
- Caller must not reuse that buffer until `on_done`.

2) Driver-owned copy (safer, small overhead)
- `i2c_dma_submit()` copies `cmds` into an internal ring/pool.
- Requires a fixed pool and failure mode when pool is exhausted.

For a generalized solution, start with **caller-owned buffers** plus a few helper macros to allocate fixed-size static buffers per device.

## Error handling and bus recovery

Handle these consistently:

- `TX_ABRT_SOURCE != 0`: NACK / arbitration / etc. Return `I2C_DMA_EABORT`.
- Bus stuck (SDA low): provide an optional “bus recover” helper (toggle SCL, issue STOP) that can be called by platform code.
- After repeated errors: reset and re-init the I2C peripheral (last resort).

## Integration steps (practical checklist)

1. Add `drv/i2c_dma.*` module.
2. Add compile-time detection for which chip headers are used (RP2040 vs RP2350) to pick correct `DREQ_I2C*_TX`.
3. Implement TX-only DMA path with:
   - submit + queue
   - DMA IRQ completion
   - STOP_DET wait + abort detection
   - timeout safety
4. Convert one existing I2C user to use the new module (as reference driver).
5. Add an optional “blocking fallback” mode:
   - if DMA channel unavailable, call regular `i2c_write_timeout_us`.
   - useful for early bring-up.
6. Document the device-driver pattern:
   - build `DATA_CMD` words
   - submit transaction(s)
   - handle completion callback

## Validation strategy

- Logic analyzer: confirm START/bytes/STOP sequence is correct; confirm STOP occurs.
- Stress test with back-to-back transfers and mixed sizes.
- Inject failure: disconnect device / wrong address to ensure abort path works and system recovers.
- Measure CPU time: main loop should remain responsive during long I2C transfers.

## Future extensions (optional)

- RX support (DMA from `data_cmd`/RX FIFO) with proper RESTART + read command words.
- Scatter/gather: multiple buffers (prefix + payload) without building a contiguous `cmds` array.
- Priority queue: allow “time-critical” I2C transactions to jump ahead (still serialized).
