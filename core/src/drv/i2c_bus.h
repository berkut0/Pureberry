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

typedef void (*i2c_bus_done_cb_t)(void *user, i2c_bus_result_t result);

bool i2c_bus_init_once(i2c_bus_id_t id);
uint32_t i2c_bus_get_baud_hz(i2c_bus_id_t id);

// Concurrency contract:
// - This module is non-reentrant.
// - Call from one main execution context (not from ISR).
// - Async completion is advanced by i2c_bus_poll() from that same context.
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

// Non-blocking transport API.
// Buffer lifetime contract:
// - `tx` and `rx` buffers (when provided) and `user` must remain valid until callback.
// Callback contract:
// - `done` must not be NULL (NULL submit is rejected with EINVAL).
// - callback is called from i2c_bus_poll() context, never from ISR.
// - callback is called exactly once for accepted submissions.
i2c_bus_result_t i2c_bus_write_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint8_t *tx,
    size_t tx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
);

i2c_bus_result_t i2c_bus_read_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    uint8_t *rx,
    size_t rx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
);

i2c_bus_result_t i2c_bus_write_read_async(
    i2c_bus_id_t id,
    uint8_t addr7,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len,
    uint32_t timeout_us,
    i2c_bus_done_cb_t done,
    void *user
);

// Compatibility API: raw transfer count / pico error values.
int i2c_bus_write_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *buf, size_t len, bool nostop);
int i2c_bus_read_timeout(i2c_bus_id_t id, uint8_t addr7, uint8_t *buf, size_t len);
int i2c_bus_write_read_timeout(i2c_bus_id_t id, uint8_t addr7, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len);

bool i2c_bus_recover(i2c_bus_id_t id);
void i2c_bus_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */
