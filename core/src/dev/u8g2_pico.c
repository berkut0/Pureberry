#include "dev/u8g2_pico.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "config.h"
#include "drv/i2c_bus.h"

// u8g2's CAD layer may send bytes one-by-one. For I2C SSD1306 we must keep them
// within a single transfer between START_TRANSFER and END_TRANSFER so that the
// control byte (0x00 command / 0x40 data) stays associated with the following bytes.
#define PICO_U8G2_I2C_TXBUF_SIZE 64
static uint8_t g_txbuf[PICO_U8G2_I2C_TXBUF_SIZE];
static uint8_t g_txlen;

static uint8_t flush_i2c(u8x8_t *u8x8, bool keep_going) {
    if (g_txlen == 0) return 1;

    uint8_t addr8 = u8x8_GetI2CAddress(u8x8);
    uint8_t addr7 = (uint8_t) (addr8 >> 1);

    i2c_bus_result_t res = i2c_bus_write((i2c_bus_id_t)OLED_I2C_BUS_ID, addr7, g_txbuf, (size_t)g_txlen, keep_going);
    g_txlen = 0;
    return (res == I2C_BUS_RESULT_OK) ? 1 : 0;
}

uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            g_txlen = 0;
            return 1;

        case U8X8_MSG_BYTE_START_TRANSFER:
            g_txlen = 0;
            return 1;

        case U8X8_MSG_BYTE_SEND: {
            uint8_t *data = (uint8_t *) arg_ptr;
            int len = (int) arg_int;
            if (len <= 0) return 1;

            // Buffer bytes until END_TRANSFER; flush with nostop if the buffer fills up.
            while (len > 0) {
                uint8_t space = (uint8_t) (PICO_U8G2_I2C_TXBUF_SIZE - g_txlen);
                if (space == 0) {
                    if (!flush_i2c(u8x8, true)) return 0;
                    space = (uint8_t) (PICO_U8G2_I2C_TXBUF_SIZE - g_txlen);
                }
                uint8_t chunk = (len < (int) space) ? (uint8_t) len : space;
                memcpy(&g_txbuf[g_txlen], data, chunk);
                g_txlen += chunk;
                data += chunk;
                len -= (int) chunk;
            }
            return 1;
        }

        case U8X8_MSG_BYTE_END_TRANSFER:
            return flush_i2c(u8x8, false);

        case U8X8_MSG_BYTE_SET_DC:
            /* Not used for I2C SSD1306 (control byte is part of the stream). */
            return 1;

        default:
            return 0;
    }
}

uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void) u8x8;
    (void) arg_ptr;

    switch (msg) {
        case U8X8_MSG_DELAY_MILLI:
            sleep_ms(arg_int);
            return 1;
        case U8X8_MSG_DELAY_10MICRO:
            sleep_us((uint64_t) arg_int * 10u);
            return 1;
        case U8X8_MSG_DELAY_NANO:
            /* Best-effort: ignore. */
            return 1;

        case U8X8_MSG_GPIO_RESET:
            /* Most SSD1306 I2C modules do not expose reset; ignore. */
            return 1;

        case U8X8_MSG_GPIO_I2C_CLOCK:
        case U8X8_MSG_GPIO_I2C_DATA:
            /* Not used with hardware I2C. */
            return 1;

        default:
            return 1;
    }
}
