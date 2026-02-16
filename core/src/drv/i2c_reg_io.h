#ifndef I2C_REG_IO_H
#define I2C_REG_IO_H

#include <stddef.h>
#include <stdint.h>

#include "drv/i2c_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef i2c_bus_result_t i2c_reg_io_result_t;

#define I2C_REG_IO_OK I2C_BUS_RESULT_OK
#define I2C_REG_IO_EINVAL I2C_BUS_RESULT_EINVAL
#define I2C_REG_IO_ENOT_INIT I2C_BUS_RESULT_ENOT_INIT
#define I2C_REG_IO_EBUSY I2C_BUS_RESULT_EBUSY
#define I2C_REG_IO_ETIMEOUT I2C_BUS_RESULT_ETIMEOUT
#define I2C_REG_IO_EIO I2C_BUS_RESULT_EIO

i2c_reg_io_result_t i2c_reg_write_u8(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint8_t value);
i2c_reg_io_result_t i2c_reg_read_u8(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint8_t *out_value);
i2c_reg_io_result_t i2c_reg_read_u16_le(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint16_t *out_value);

// Write payload bytes starting at register `reg`.
// `len` must be >0. For larger frames, tune I2C_REG_IO_MAX_STACK_TX in i2c_reg_io.c.
i2c_reg_io_result_t i2c_reg_write_seq(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len);

// Read-modify-write helper: keeps unmasked bits unchanged.
i2c_reg_io_result_t i2c_reg_update_bits(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint8_t mask, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* I2C_REG_IO_H */
