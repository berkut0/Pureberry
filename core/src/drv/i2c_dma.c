/*
 * I2C transport via DMA-fed IC_DATA_CMD stream.
 *
 * This implementation drives the DW_apb_i2c controller by DMA-writing words to IC_DATA_CMD.
 * DMA completion only means "all words were written to the TX FIFO", not "STOP happened on
 * the bus", so we also wait for STOP_DET (or detect TX_ABRT) before completing a transaction.
 */

#include "drv/i2c_dma.h"

#include <string.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/regs/dreq.h"
#include "hardware/regs/i2c.h"

static i2c_dma_t *g_dma_by_channel[NUM_DMA_CHANNELS];
static bool g_dma_irq_installed[2];

static inline uint i2c_dma_dreq_for_inst(i2c_inst_t *i2c) {
    if (i2c == i2c0) return DREQ_I2C0_TX;
    return DREQ_I2C1_TX;
}

static void __isr i2c_dma_irq_common(uint8_t irq_index) {
    // Process all channels that triggered this DMA IRQ.
    uint32_t pending = dma_hw->irq_ctrl[irq_index].ints;
    while (pending) {
        uint chan = (uint)__builtin_ctz(pending);
        pending &= (pending - 1u);

        i2c_dma_t *ctx = (chan < NUM_DMA_CHANNELS) ? g_dma_by_channel[chan] : NULL;
        dma_irqn_acknowledge_channel(irq_index, chan);
        if (!ctx) continue;
        if (ctx->dma_irq_index != irq_index) continue;

        // Ignore stale IRQs from a transaction already completed by poll fallback.
        // Only an actively running DMA transfer may transition to WAIT_STOP.
        if (ctx->state == I2C_DMA_STATE_DMA_ACTIVE && ctx->current.cmd_count > 0u) {
            // DMA finished pushing words into the TX FIFO. Now wait for STOP.
            ctx->state = I2C_DMA_STATE_WAIT_STOP;
        }
    }
}

static void __isr i2c_dma_irq0(void) { i2c_dma_irq_common(0); }
static void __isr i2c_dma_irq1(void) { i2c_dma_irq_common(1); }

static void i2c_dma_install_irq(uint8_t irq_index) {
    if (irq_index > 1) irq_index = 1;
    if (g_dma_irq_installed[irq_index]) return;
    g_dma_irq_installed[irq_index] = true;

    irq_handler_t handler = (irq_index == 0) ? i2c_dma_irq0 : i2c_dma_irq1;
    irq_add_shared_handler(DMA_IRQ_0 + irq_index, handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0 + irq_index, true);
}

static void i2c_dma_clear_i2c_irq_latches(i2c_dma_t *ctx) {
    // Clear STOP_DET and TX_ABRT latches so we don't see stale state.
    (void)ctx->hw->clr_stop_det;
    (void)ctx->hw->clr_tx_abrt;
    (void)ctx->hw->clr_intr;
}

static void i2c_dma_abort_active(i2c_dma_t *ctx, i2c_dma_result_t result) {
    // Best-effort abort. DMA may already be idle; this is safe.
    if (ctx->dma_chan >= 0) {
        dma_channel_abort((uint)ctx->dma_chan);
        dma_irqn_acknowledge_channel(ctx->dma_irq_index, (uint)ctx->dma_chan);
        dma_irqn_set_channel_enabled(ctx->dma_irq_index, (uint)ctx->dma_chan, false);
    }

    // Reset the I2C controller to a known state.
    ctx->hw->enable = 0;
    ctx->hw->enable = 1;
    i2c_dma_clear_i2c_irq_latches(ctx);

    i2c_dma_done_cb_t done = ctx->current.done;
    void *done_user = ctx->current.user;

    ctx->state = I2C_DMA_STATE_IDLE;
    ctx->deadline_valid = false;
    ctx->rx_received = 0u;
    memset(&ctx->current, 0, sizeof(ctx->current));

    if (done) {
        done(done_user, result);
    }
}

static void i2c_dma_complete_active(i2c_dma_t *ctx, i2c_dma_result_t result) {
    if (ctx->dma_chan >= 0) {
        // Clear any pending channel IRQ to avoid stale post-completion state flips.
        dma_irqn_acknowledge_channel(ctx->dma_irq_index, (uint)ctx->dma_chan);
        dma_irqn_set_channel_enabled(ctx->dma_irq_index, (uint)ctx->dma_chan, false);
    }
    i2c_dma_done_cb_t done = ctx->current.done;
    void *done_user = ctx->current.user;

    ctx->state = I2C_DMA_STATE_IDLE;
    ctx->deadline_valid = false;
    ctx->rx_received = 0u;
    memset(&ctx->current, 0, sizeof(ctx->current));

    if (done) {
        done(done_user, result);
    }
}

static bool i2c_dma_deadline_expired(i2c_dma_t *ctx) {
    if (!ctx->deadline_valid) return false;
    return absolute_time_diff_us(get_absolute_time(), ctx->deadline) < 0;
}

static bool i2c_dma_drain_rx(i2c_dma_t *ctx) {
    bool overflow = false;
    while (ctx->hw->rxflr != 0u) {
        uint32_t v = ctx->hw->data_cmd;
        if (ctx->rx_received < ctx->current.rx_count && ctx->current.rx) {
            ctx->current.rx[ctx->rx_received++] = (uint8_t)(v & 0xFFu);
        } else {
            overflow = true;
        }
    }
    return !overflow;
}

static void i2c_dma_begin_txn(i2c_dma_t *ctx, i2c_dma_txn_t const *t) {
    ctx->current = *t;
    ctx->rx_received = 0u;
    ctx->last_abort_source = 0u;

    if (t->timeout_us > 0u) {
        ctx->deadline_valid = true;
        ctx->deadline = make_timeout_time_us(t->timeout_us);
    } else {
        ctx->deadline_valid = false;
    }

    // Configure I2C target address (same pattern as pico-sdk i2c_write_* helpers).
    ctx->hw->enable = 0;
    ctx->hw->tar = (uint32_t)t->addr7;
    ctx->hw->enable = 1;
    i2c_dma_clear_i2c_irq_latches(ctx);

    // Configure DMA channel.
    dma_channel_config c = dma_channel_get_default_config((uint)ctx->dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, i2c_dma_dreq_for_inst(ctx->i2c));

    dma_channel_configure(
        (uint)ctx->dma_chan,
        &c,
        &ctx->hw->data_cmd,        // write addr
        ctx->current.cmds,         // read addr
        ctx->current.cmd_count,    // transfer count
        false                      // start later (after IRQ enable)
    );

    // Enable DMA interrupt and start.
    dma_irqn_acknowledge_channel(ctx->dma_irq_index, (uint)ctx->dma_chan);
    dma_irqn_set_channel_enabled(ctx->dma_irq_index, (uint)ctx->dma_chan, true);
    dma_channel_start((uint)ctx->dma_chan);

    ctx->state = I2C_DMA_STATE_DMA_ACTIVE;
}

void i2c_dma_init(i2c_dma_t *ctx, i2c_inst_t *i2c, int dma_chan, uint8_t dma_irq_index) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));

    ctx->i2c = i2c;
    ctx->hw = i2c ? i2c_get_hw(i2c) : NULL;
    ctx->dma_irq_index = (dma_irq_index > 1) ? 1 : dma_irq_index;

    if (!ctx->i2c || !ctx->hw) {
        ctx->dma_chan = -1;
        return;
    }

    if (dma_chan < 0) {
        ctx->dma_chan = (int)dma_claim_unused_channel(false);
    } else if (dma_chan >= (int)NUM_DMA_CHANNELS) {
        ctx->dma_chan = -1;
    } else if (dma_channel_is_claimed((uint)dma_chan)) {
        ctx->dma_chan = -1;
    } else {
        ctx->dma_chan = dma_chan;
        dma_channel_claim((uint)dma_chan);
    }

    if (ctx->dma_chan >= 0 && ctx->dma_chan < (int)NUM_DMA_CHANNELS) {
        g_dma_by_channel[ctx->dma_chan] = ctx;
    }

    if (ctx->dma_chan >= 0) {
        i2c_dma_install_irq(ctx->dma_irq_index);
    }

    ctx->state = I2C_DMA_STATE_IDLE;
    i2c_dma_clear_i2c_irq_latches(ctx);
}

bool i2c_dma_submit(i2c_dma_t *ctx, const i2c_dma_txn_t *txn) {
    if (!ctx || !txn) return false;
    if (!ctx->i2c || !ctx->hw) return false;
    if (ctx->dma_chan < 0) return false;
    if (!txn->cmds || txn->cmd_count == 0) return false;
    if (txn->addr7 >= 0x80) return false;
    if (txn->rx_count > 0u && !txn->rx) return false;
    if (txn->rx_count > txn->cmd_count) return false;
    if (ctx->state != I2C_DMA_STATE_IDLE) return false;
    if (ctx->current.cmd_count != 0u) return false;
    if (dma_channel_is_busy((uint)ctx->dma_chan)) return false;

    i2c_dma_begin_txn(ctx, txn);
    return true;
}

void i2c_dma_poll(i2c_dma_t *ctx) {
    if (!ctx || !ctx->i2c || !ctx->hw) return;

    // Recover from any stale state left by a late IRQ after completion.
    if (ctx->state != I2C_DMA_STATE_IDLE && ctx->current.cmd_count == 0u) {
        ctx->state = I2C_DMA_STATE_IDLE;
        ctx->deadline_valid = false;
    }

    if (ctx->state == I2C_DMA_STATE_IDLE) {
        return;
    }

    if (i2c_dma_deadline_expired(ctx)) {
        i2c_dma_abort_active(ctx, I2C_DMA_RESULT_ETIMEOUT);
        return;
    }

    uint32_t raw = ctx->hw->raw_intr_stat;
    if (raw & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
        ctx->last_abort_source = ctx->hw->tx_abrt_source;
        (void)ctx->hw->clr_tx_abrt;
        i2c_dma_abort_active(ctx, I2C_DMA_RESULT_EABORT);
        return;
    }

    if (!i2c_dma_drain_rx(ctx)) {
        i2c_dma_abort_active(ctx, I2C_DMA_RESULT_EABORT);
        return;
    }

    // If DMA IRQ is missed for any reason, fall back to polling DMA busy.
    if (ctx->state == I2C_DMA_STATE_DMA_ACTIVE && !dma_channel_is_busy((uint)ctx->dma_chan)) {
        ctx->state = I2C_DMA_STATE_WAIT_STOP;
    }

    if (ctx->state == I2C_DMA_STATE_WAIT_STOP) {
        raw = ctx->hw->raw_intr_stat;
        if (raw & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) {
            (void)ctx->hw->clr_stop_det;
            if (!i2c_dma_drain_rx(ctx) || ctx->rx_received != ctx->current.rx_count) {
                i2c_dma_abort_active(ctx, I2C_DMA_RESULT_EABORT);
            } else {
                i2c_dma_complete_active(ctx, I2C_DMA_RESULT_OK);
            }
        }
    }
}

bool i2c_dma_busy(const i2c_dma_t *ctx) {
    if (!ctx) return false;
    if (ctx->state != I2C_DMA_STATE_IDLE) return true;
    return (ctx->dma_chan >= 0 && dma_channel_is_busy((uint)ctx->dma_chan));
}

uint32_t i2c_dma_get_last_abort_source(const i2c_dma_t *ctx) {
    return ctx ? ctx->last_abort_source : 0u;
}

size_t i2c_dma_build_write_cmds(
    uint32_t *out_cmds,
    size_t out_cap,
    const uint8_t *bytes,
    size_t len,
    bool restart_first,
    bool stop_last
) {
    if (!out_cmds || !bytes) return 0;
    if (len == 0) return 0;
    if (out_cap < len) return 0;

    for (size_t i = 0; i < len; i++) {
        uint32_t v = (uint32_t)bytes[i] & 0xFFu;
        if (restart_first && i == 0) v |= I2C_IC_DATA_CMD_RESTART_BITS;
        if (stop_last && i == (len - 1)) v |= I2C_IC_DATA_CMD_STOP_BITS;
        out_cmds[i] = v;
    }
    return len;
}
