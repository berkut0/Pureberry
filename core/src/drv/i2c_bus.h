#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/types.h"
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_BUS_ID_0 = 0,
    I2C_BUS_ID_1 = 1,
    I2C_BUS_ID_COUNT
} i2c_bus_id_t;

typedef struct {
    i2c_inst_t *inst;
    uint sda_pin;
    uint scl_pin;
    uint32_t baud_hz;
    uint32_t timeout_us;
} i2c_bus_config_t;

const i2c_bus_config_t *i2c_bus_get_config(i2c_bus_id_t id);
bool i2c_bus_init_once(i2c_bus_id_t id);
i2c_inst_t *i2c_bus_get_inst(i2c_bus_id_t id);

int i2c_bus_write_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop);
int i2c_bus_read_timeout(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len);
int i2c_bus_write_read_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len);

void i2c_bus_recover(i2c_bus_id_t id);
void i2c_bus_poll(void);

#ifdef ENABLE_I2C_DMA
#include "drv/i2c_dma.h"
bool i2c_bus_dma_init(i2c_bus_id_t id, int dma_chan, uint8_t dma_irq_index);
bool i2c_bus_dma_ready(i2c_bus_id_t id);
bool i2c_bus_dma_busy(i2c_bus_id_t id);
bool i2c_bus_dma_submit(i2c_bus_id_t id, const i2c_dma_txn_t *txn);
#endif

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */
