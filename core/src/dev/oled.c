#include "dev/oled.h"

#ifdef ENABLE_OLED

#include <string.h>

#include "pico/stdlib.h"

#include "config.h"
#include "dev/u8g2_pico.h"
#include "drv/i2c_bus.h"

#ifdef ENABLE_I2C_DMA
#include "hardware/regs/i2c.h"
#endif

static u8g2_t g_u8g2;
static bool g_oled_ready;

#ifdef ENABLE_I2C_DMA
static bool g_oled_dma_ready;
static uint32_t g_oled_cmd_window[7];

enum { OLED_FB_LEN = OLED_WIDTH * (OLED_HEIGHT / 8) };
static uint32_t g_oled_cmd_data[1 + OLED_FB_LEN];

static bool oled_dma_prepare_window_cmds(void) {
    uint8_t const window_bytes[7] = {
        0x00,                                       // control byte: command stream
        0x21, 0x00, (uint8_t)(OLED_WIDTH - 1),       // column address
        0x22, 0x00, (uint8_t)((OLED_HEIGHT / 8) - 1) // page address
    };

    return i2c_dma_build_write_cmds(
               g_oled_cmd_window,
               sizeof(g_oled_cmd_window) / sizeof(g_oled_cmd_window[0]),
               window_bytes,
               sizeof(window_bytes),
               false,
               true) == (sizeof(g_oled_cmd_window) / sizeof(g_oled_cmd_window[0]));
}

static bool oled_queue_full_frame_dma(void) {
    if (!g_oled_dma_ready) return false;
    if (i2c_bus_dma_busy(OLED_I2C_BUS_ID)) return false;

    // Build data transaction: control byte 0x40 + framebuffer bytes.
    uint8_t const *fb = u8g2_GetBufferPtr(&g_u8g2);
    if (!fb) return false;
    g_oled_cmd_data[0] = 0x40u; // control byte: data stream
    for (size_t i = 0; i < (size_t)OLED_FB_LEN; i++) {
        uint32_t v = (uint32_t)fb[i] & 0xFFu;
        if (i == (size_t)(OLED_FB_LEN - 1)) {
            v |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        g_oled_cmd_data[1 + i] = v;
    }

    // Queue window, then data. Use a generous timeout for the full-frame transfer.
    i2c_dma_txn_t t1 = {
        .addr7 = (uint8_t)OLED_I2C_ADDR,
        .cmds = g_oled_cmd_window,
        .cmd_count = sizeof(g_oled_cmd_window) / sizeof(g_oled_cmd_window[0]),
        .timeout_us = 20000u,
        .done = NULL,
        .user = NULL,
    };
    i2c_dma_txn_t t2 = {
        .addr7 = (uint8_t)OLED_I2C_ADDR,
        .cmds = g_oled_cmd_data,
        .cmd_count = sizeof(g_oled_cmd_data) / sizeof(g_oled_cmd_data[0]),
        .timeout_us = 50000u,
        .done = NULL,
        .user = NULL,
    };

    if (!i2c_bus_dma_submit(OLED_I2C_BUS_ID, &t1)) return false;
    if (!i2c_bus_dma_submit(OLED_I2C_BUS_ID, &t2)) return false;
    return true;
}
#endif

void oled_backend_flush(void) {
    if (!g_oled_ready) return;

#ifdef ENABLE_I2C_DMA
    if (g_oled_dma_ready) {
        (void)oled_queue_full_frame_dma();
        return;
    }
#endif
    u8g2_SendBuffer(&g_u8g2);
}

static i2c_inst_t *oled_get_i2c(void) {
    return i2c_bus_get_inst((i2c_bus_id_t)OLED_I2C_BUS_ID);
}

static void oled_draw_boot(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&g_u8g2, 0, 12, "SSD1306 OLED");
    u8g2_DrawStr(&g_u8g2, 0, 26, "u8g2 + Pico");
    oled_backend_flush();
}

bool oled_backend_init(void) {
    if (g_oled_ready) return true;

    // If the bus is stuck (e.g., SDA held low), recover first so init cannot hang core0.
    i2c_bus_recover((i2c_bus_id_t)OLED_I2C_BUS_ID);

    if (!i2c_bus_init_once((i2c_bus_id_t)OLED_I2C_BUS_ID)) {
        return false;
    }

    i2c_inst_t *i2c = oled_get_i2c();
    if (!i2c) {
        return false;
    }

#ifdef ENABLE_I2C_DMA
    // I2C DMA is used only for OLED refresh (TX). u8g2 init remains blocking and is OK at boot.
    g_oled_dma_ready = i2c_bus_dma_init((i2c_bus_id_t)OLED_I2C_BUS_ID, -1, 1) && oled_dma_prepare_window_cmds();
#endif

    // Setup u8g2 for SSD1306 I2C 128x64 (full framebuffer)
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&g_u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
    u8x8_SetUserPtr(u8g2_GetU8x8(&g_u8g2), i2c);
    u8x8_SetI2CAddress(u8g2_GetU8x8(&g_u8g2), (uint8_t) (OLED_I2C_ADDR << 1));

    // u8g2 init will perform I2C transactions; our backend uses timeouts to avoid hangs.
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

    oled_draw_boot();

    g_oled_ready = true;
    return true;
}

bool oled_backend_ready(void) {
    return g_oled_ready;
}

oled_canvas_t *oled_backend_u8g2(void) {
    if (!g_oled_ready) return NULL;
    return &g_u8g2;
}

#else

bool oled_backend_init(void) { return false; }
bool oled_backend_ready(void) { return false; }
oled_canvas_t *oled_backend_u8g2(void) { return NULL; }
void oled_backend_flush(void) { (void) 0; }

#endif // ENABLE_OLED
