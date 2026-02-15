#include "dev/oled.h"

#ifdef ENABLE_OLED

#include <string.h>

#include "pico/stdlib.h"

#include "config.h"
#include "dev/u8g2_pico.h"
#include "drv/i2c_bus.h"

static u8g2_t g_u8g2;
static bool g_oled_ready;
enum { OLED_FB_LEN = OLED_WIDTH * (OLED_HEIGHT / 8) };
static const uint8_t g_oled_window_bytes[7] = {
    0x00,                                       // control byte: command stream
    0x21, 0x00, (uint8_t)(OLED_WIDTH - 1),      // column address
    0x22, 0x00, (uint8_t)((OLED_HEIGHT / 8) - 1) // page address
};
static uint8_t g_oled_data_bytes[1 + OLED_FB_LEN];
static bool g_oled_async_inflight;

static void oled_async_data_done(void *user, i2c_bus_result_t result) {
    (void)user;
    (void)result;
    g_oled_async_inflight = false;
}

static void oled_async_window_done(void *user, i2c_bus_result_t result) {
    (void)user;
    if (result != I2C_BUS_RESULT_OK) {
        g_oled_async_inflight = false;
        return;
    }

    i2c_bus_result_t r = i2c_bus_write_async(
        OLED_I2C_BUS_ID,
        (uint8_t)OLED_I2C_ADDR,
        g_oled_data_bytes,
        sizeof(g_oled_data_bytes),
        50000u,
        oled_async_data_done,
        NULL
    );
    if (r != I2C_BUS_RESULT_OK) {
        g_oled_async_inflight = false;
    }
}

static bool oled_queue_full_frame_async(void) {
    if (g_oled_async_inflight) return false;

    // Build data transaction: control byte 0x40 + framebuffer bytes.
    uint8_t const *fb = u8g2_GetBufferPtr(&g_u8g2);
    if (!fb) return false;
    g_oled_data_bytes[0] = 0x40u; // control byte: data stream
    memcpy(&g_oled_data_bytes[1], fb, OLED_FB_LEN);

    g_oled_async_inflight = true;
    i2c_bus_result_t r = i2c_bus_write_async(
        OLED_I2C_BUS_ID,
        (uint8_t)OLED_I2C_ADDR,
        g_oled_window_bytes,
        sizeof(g_oled_window_bytes),
        20000u,
        oled_async_window_done,
        NULL
    );
    if (r != I2C_BUS_RESULT_OK) {
        g_oled_async_inflight = false;
        return false;
    }
    return true;
}

void oled_backend_flush(void) {
    if (!g_oled_ready) return;
    (void)oled_queue_full_frame_async();
}

static void oled_draw_boot(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&g_u8g2, 0, 12, "SSD1306 OLED");
    u8g2_DrawStr(&g_u8g2, 0, 26, "u8g2 + Pico");
    u8g2_SendBuffer(&g_u8g2);
}

bool oled_backend_init(void) {
    if (g_oled_ready) return true;

    // If the bus is stuck (e.g., SDA held low), recover first so init cannot hang core0.
    (void)i2c_bus_recover((i2c_bus_id_t)OLED_I2C_BUS_ID);

    if (!i2c_bus_init_once((i2c_bus_id_t)OLED_I2C_BUS_ID)) {
        return false;
    }

    // Setup u8g2 for SSD1306 I2C 128x64 (full framebuffer)
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&g_u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
    u8x8_SetI2CAddress(u8g2_GetU8x8(&g_u8g2), (uint8_t) (OLED_I2C_ADDR << 1));

    // u8g2 init will perform I2C transactions; our backend uses timeouts to avoid hangs.
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

    oled_draw_boot();

    g_oled_async_inflight = false;
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
