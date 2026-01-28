#ifndef U8G2_PICO_H
#define U8G2_PICO_H

#include <stdint.h>
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * u8g2 Pico SDK backend callbacks (hardware I2C).
 *
 * Usage:
 * - Call u8g2_Setup_ssd1306_i2c_128x64_noname_f(..., u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico)
 * - Set the I2C address with u8x8_SetI2CAddress(u8g2_GetU8x8(&u8g2), addr<<1)
 * - Store an i2c_inst_t* in u8x8 user ptr (u8x8_SetUserPtr)
 */

uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#ifdef __cplusplus
}
#endif

#endif // U8G2_PICO_H
