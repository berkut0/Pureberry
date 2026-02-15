#include "drv/i2c_reg_io.h"

#include <string.h>

#ifndef I2C_REG_IO_MAX_STACK_TX
#define I2C_REG_IO_MAX_STACK_TX 64u
#endif

i2c_reg_io_result_t i2c_reg_write_u8(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint8_t value) {
    uint8_t tx[2] = { reg, value };
    return i2c_bus_write(bus_id, addr7, tx, sizeof(tx), false);
}

i2c_reg_io_result_t i2c_reg_read_u8(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint8_t *out_value) {
    if (!out_value) {
        return I2C_REG_IO_EINVAL;
    }

    return i2c_bus_write_read(bus_id, addr7, &reg, 1u, out_value, 1u);
}

i2c_reg_io_result_t i2c_reg_read_u16_le(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint16_t *out_value) {
    if (!out_value) {
        return I2C_REG_IO_EINVAL;
    }

    uint8_t rx[2];
    i2c_reg_io_result_t res = i2c_bus_write_read(bus_id, addr7, &reg, 1u, rx, sizeof(rx));
    if (res != I2C_REG_IO_OK) {
        return res;
    }

    *out_value = (uint16_t)(((uint16_t)rx[1] << 8) | (uint16_t)rx[0]);
    return I2C_REG_IO_OK;
}

i2c_reg_io_result_t i2c_reg_write_seq(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len) {
    if (!data || len == 0u) {
        return I2C_REG_IO_EINVAL;
    }
    if (len > (I2C_REG_IO_MAX_STACK_TX - 1u)) {
        return I2C_REG_IO_EINVAL;
    }

    uint8_t tx[I2C_REG_IO_MAX_STACK_TX];
    tx[0] = reg;
    memcpy(&tx[1], data, len);

    size_t tx_len = len + 1u;
    return i2c_bus_write(bus_id, addr7, tx, tx_len, false);
}

i2c_reg_io_result_t i2c_reg_update_bits(i2c_bus_id_t bus_id, uint8_t addr7, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t old_value = 0u;
    i2c_reg_io_result_t res = i2c_reg_read_u8(bus_id, addr7, reg, &old_value);
    if (res != I2C_REG_IO_OK) {
        return res;
    }

    uint8_t new_value = (uint8_t)((old_value & (uint8_t)(~mask)) | (value & mask));
    if (new_value == old_value) {
        return I2C_REG_IO_OK;
    }

    return i2c_reg_write_u8(bus_id, addr7, reg, new_value);
}
