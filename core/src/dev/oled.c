#include "dev/oled.h"

#ifdef ENABLE_OLED

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "config.h"
#include "dev/u8g2_pico.h"
#include "multicore_display.h"

static u8g2_t g_u8g2;
static bool g_oled_ready;
static absolute_time_t g_next_frame;
static uint32_t g_frame_counter;

static i2c_inst_t *oled_get_i2c(void) {
#if OLED_I2C_INSTANCE == 0
    return i2c0;
#else
    return i2c1;
#endif
}

static void oled_i2c_bus_recover(void) {
    // Basic I2C bus recovery:
    // If a slave is holding SDA low, toggle SCL to advance it, then issue a STOP.
    gpio_init(OLED_I2C_SDA_PIN);
    gpio_init(OLED_I2C_SCL_PIN);
    gpio_pull_up(OLED_I2C_SDA_PIN);
    gpio_pull_up(OLED_I2C_SCL_PIN);

    gpio_set_dir(OLED_I2C_SDA_PIN, GPIO_IN);
    gpio_set_dir(OLED_I2C_SCL_PIN, GPIO_IN);
    sleep_us(5);

    if (gpio_get(OLED_I2C_SDA_PIN) != 0) {
        return; // bus not stuck
    }

    // Clock out up to 9 pulses on SCL.
    gpio_set_dir(OLED_I2C_SCL_PIN, GPIO_OUT);
    for (int i = 0; i < 9; i++) {
        gpio_put(OLED_I2C_SCL_PIN, 0);
        sleep_us(5);
        gpio_put(OLED_I2C_SCL_PIN, 1);
        sleep_us(5);
        if (gpio_get(OLED_I2C_SDA_PIN) != 0) break;
    }

    // Generate STOP: SDA low -> SCL high -> SDA high.
    gpio_set_dir(OLED_I2C_SDA_PIN, GPIO_OUT);
    gpio_put(OLED_I2C_SDA_PIN, 0);
    sleep_us(5);
    gpio_put(OLED_I2C_SCL_PIN, 1);
    sleep_us(5);
    gpio_put(OLED_I2C_SDA_PIN, 1);
    sleep_us(5);

    // Release lines.
    gpio_set_dir(OLED_I2C_SDA_PIN, GPIO_IN);
    gpio_set_dir(OLED_I2C_SCL_PIN, GPIO_IN);
}

static void oled_draw_boot(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&g_u8g2, 0, 12, "SSD1306 OLED");
    u8g2_DrawStr(&g_u8g2, 0, 26, "u8g2 + Pico");
    u8g2_SendBuffer(&g_u8g2);
}

static void oled_draw_waveform(const uint8_t y[128]) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);

    char line[32];
    snprintf(line, sizeof(line), "frame %lu", (unsigned long) g_frame_counter);
    u8g2_DrawStr(&g_u8g2, 0, 10, line);

    // Draw center line (y = 32) and waveform below the header area.
    const uint8_t y_center = (uint8_t) (OLED_HEIGHT / 2);
    u8g2_DrawHLine(&g_u8g2, 0, y_center, OLED_WIDTH);

    uint8_t prev_y = y[0];
    for (int x = 0; x < 128; x++) {
        uint8_t yy = y[x];
        if (x > 0) {
            u8g2_DrawLine(&g_u8g2, x - 1, prev_y, x, yy);
        } else {
            u8g2_DrawPixel(&g_u8g2, x, yy);
        }
        prev_y = yy;
    }

    u8g2_SendBuffer(&g_u8g2);
}

bool oled_init(void) {
    i2c_inst_t *i2c = oled_get_i2c();

    // If the bus is stuck (e.g., SDA held low), recover first so init cannot hang core0.
    oled_i2c_bus_recover();

    // I2C pins
    gpio_set_function(OLED_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_I2C_SDA_PIN);
    gpio_pull_up(OLED_I2C_SCL_PIN);

    i2c_init(i2c, OLED_I2C_BAUD);

    // Setup u8g2 for SSD1306 I2C 128x64 (full framebuffer)
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&g_u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
    u8x8_SetUserPtr(u8g2_GetU8x8(&g_u8g2), i2c);
    u8x8_SetI2CAddress(u8g2_GetU8x8(&g_u8g2), (uint8_t) (OLED_I2C_ADDR << 1));

    // u8g2 init will perform I2C transactions; our backend uses timeouts to avoid hangs.
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

    oled_draw_boot();

    g_oled_ready = true;
    g_next_frame = make_timeout_time_ms(250);
    g_frame_counter = 0;
    return true;
}

void oled_task(void) {
    if (!g_oled_ready) return;

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, g_next_frame) > 0) {
        return; // not yet
    }
    g_next_frame = delayed_by_ms(now, 1000 / OLED_REFRESH_FPS);
    g_frame_counter++;

    uint8_t y[128];
    if (multicore_display_read_latest(y)) {
        oled_draw_waveform(y);
    } else {
        // No waveform yet: keep showing a small animation.
        u8g2_ClearBuffer(&g_u8g2);
        u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
        u8g2_DrawStr(&g_u8g2, 0, 12, "Waiting waveform...");
        u8g2_DrawFrame(&g_u8g2, 0, 20, OLED_WIDTH, OLED_HEIGHT - 20);
        int x = (int) (g_frame_counter % (OLED_WIDTH - 6));
        u8g2_DrawBox(&g_u8g2, x, 24, 6, 6);
        u8g2_SendBuffer(&g_u8g2);
    }
}

#else

bool oled_init(void) { return false; }
void oled_task(void) { (void)0; }

#endif // ENABLE_OLED

