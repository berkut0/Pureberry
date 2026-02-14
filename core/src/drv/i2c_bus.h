#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_BUS_ID_0 = 0,
    I2C_BUS_ID_1 = 1,
    I2C_BUS_ID_COUNT
} i2c_bus_id_t;

typedef enum {
    I2C_BUS_RESULT_OK = 0,
    I2C_BUS_RESULT_EINVAL,
    I2C_BUS_RESULT_ENOT_INIT,
    I2C_BUS_RESULT_EBUSY,
    I2C_BUS_RESULT_ETIMEOUT,
    I2C_BUS_RESULT_EIO,
} i2c_bus_result_t;

bool i2c_bus_init_once(i2c_bus_id_t id);
uint32_t i2c_bus_get_baud_hz(i2c_bus_id_t id);

// Concurrency contract:
// - This module is non-reentrant.
// - Call from one main execution context (not from ISR).
// - DMA completion is advanced by i2c_bus_poll() from that same context.
// Preferred device-layer API: status-oriented helpers.
i2c_bus_result_t i2c_bus_write(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop);
i2c_bus_result_t i2c_bus_read(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len);
i2c_bus_result_t i2c_bus_write_read(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len
);

// Compatibility API: raw transfer count / pico error values.
int i2c_bus_write_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop);
int i2c_bus_read_timeout(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len);
int i2c_bus_write_read_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len);

bool i2c_bus_recover(i2c_bus_id_t id);
void i2c_bus_poll(void);

#ifdef ENABLE_I2C_DMA
i2c_bus_result_t i2c_bus_dma_init(i2c_bus_id_t id, int dma_chan, uint8_t dma_irq_index);
bool i2c_bus_dma_ready(i2c_bus_id_t id);
bool i2c_bus_dma_busy(i2c_bus_id_t id);
typedef struct {
    const uint8_t *bytes;
    size_t len;
    bool restart_first;
    uint32_t timeout_us;
    uint32_t *cmd_buf;
    size_t cmd_buf_words;
} i2c_bus_dma_write_req_t;

// Submit an ordered sequence of DMA write requests.
// - Converts request bytes into IC_DATA_CMD words inside i2c_bus.
// - Caller owns cmd_buf storage and must keep it valid until transfer completion.
// - Requires DMA engine idle before queueing the sequence.
i2c_bus_result_t i2c_bus_dma_submit_writes(
    i2c_bus_id_t id,
    uint8_t addr7,
    const i2c_bus_dma_write_req_t *reqs,
    size_t req_count
);
#endif

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */
