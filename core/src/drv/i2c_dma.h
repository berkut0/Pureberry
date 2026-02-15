/*
 * I2C transport helper via DMA-fed IC_DATA_CMD stream.
 *
 * Goals:
 * - Non-blocking I2C transactions (write/read/write_read).
 * - Reusable across multiple I2C peripherals on the same bus.
 * - Safe-by-default: serialized per i2c_inst_t, timeout-protected, error-reported.
 *
 * Notes:
 * - Callers provide a DATA_CMD command stream (one entry per bus byte operation) so
 *   STOP/RESTART/read commands can be expressed without protocol-specific branching.
 */

#ifndef I2C_DMA_H
#define I2C_DMA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "hardware/i2c.h"
#include "hardware/structs/i2c.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_DMA_RESULT_OK = 0,
    I2C_DMA_RESULT_EINVAL,
    I2C_DMA_RESULT_EQUEUE_FULL,
    I2C_DMA_RESULT_EABORT,
    I2C_DMA_RESULT_ETIMEOUT,
} i2c_dma_result_t;

typedef void (*i2c_dma_done_cb_t)(void *user, i2c_dma_result_t result);

typedef struct {
    uint8_t addr7;

    // Stream of words written to IC_DATA_CMD (one entry per I2C byte).
    // The low 8 bits are the byte; STOP/RESTART bits are set as needed.
    //
    // Lifetime: must remain valid until completion callback is invoked.
    const uint32_t *cmds;
    size_t cmd_count;

    // Optional timeout for the entire transaction (0 = no timeout).
    uint32_t timeout_us;

    // Optional RX destination.
    // Lifetime: must remain valid until completion callback is invoked.
    uint8_t *rx;
    size_t rx_count;

    // Optional completion callback (invoked from i2c_dma_poll, not from IRQ).
    i2c_dma_done_cb_t done;
    void *user;
} i2c_dma_txn_t;

typedef enum {
    I2C_DMA_STATE_IDLE = 0,
    I2C_DMA_STATE_DMA_ACTIVE,
    I2C_DMA_STATE_WAIT_STOP,
} i2c_dma_state_t;

typedef struct i2c_dma {
    i2c_inst_t *i2c;
    i2c_hw_t *hw;

    int dma_chan;
    uint8_t dma_irq_index;

    volatile i2c_dma_state_t state;

    bool deadline_valid;
    absolute_time_t deadline;

    i2c_dma_txn_t current;
    size_t rx_received;

    queue_t q;

    uint32_t last_abort_source;
} i2c_dma_t;

// dma_chan: pass -1 to claim an unused DMA channel automatically.
// dma_irq_index: 0 or 1 (recommend 1 to avoid sharing DMA IRQ with audio DMA).
// If no DMA channel is available, ctx->dma_chan remains -1 and i2c_dma_submit() will fail.
void i2c_dma_init(i2c_dma_t *ctx, i2c_inst_t *i2c, int dma_chan, uint8_t dma_irq_index);

// Enqueue a transaction; returns false if the queue is full or txn invalid.
bool i2c_dma_submit(i2c_dma_t *ctx, const i2c_dma_txn_t *txn);

// Drive completion state machine and invoke callbacks. Call frequently from core0 main loop.
void i2c_dma_poll(i2c_dma_t *ctx);

// True if a transaction is active or queued.
bool i2c_dma_busy(const i2c_dma_t *ctx);

// Returns the last TX_ABRT_SOURCE value observed (0 if none).
uint32_t i2c_dma_get_last_abort_source(const i2c_dma_t *ctx);

// Convenience builder: convert raw bytes to IC_DATA_CMD words.
// - restart_first: set RESTART on the first byte
// - stop_last: set STOP on the last byte
// Returns number of words written (0 on error).
size_t i2c_dma_build_write_cmds(
    uint32_t *out_cmds,
    size_t out_cap,
    const uint8_t *bytes,
    size_t len,
    bool restart_first,
    bool stop_last
);

#ifdef __cplusplus
}
#endif

#endif // I2C_DMA_H
