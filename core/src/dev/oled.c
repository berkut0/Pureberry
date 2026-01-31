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
#include "crash.h"

#if defined(ENABLE_USB_MIDI) || defined(ENABLE_USB_AUDIO)
#include "tusb.h"
#endif

#ifdef ENABLE_USB_AUDIO
#include "usb/usb_audio.h"
#endif

static u8g2_t g_u8g2;
static bool g_oled_ready;
static absolute_time_t g_next_frame;
static uint32_t g_frame_counter;

#define OLED_CRASH_SCREEN_MS 5000
static bool g_show_crash_screen;
static absolute_time_t g_crash_screen_until;
static crash_info_t g_crash_info;

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

static inline void oled_usb_service(void) {
#if defined(ENABLE_USB_MIDI) || defined(ENABLE_USB_AUDIO)
    tud_task();
#ifdef ENABLE_USB_AUDIO
    usb_audio_task();
#endif
#endif
}

static bool oled_i2c_write_timeout(const uint8_t *data, size_t len) {
    if (len == 0) return true;
    i2c_inst_t *i2c = oled_get_i2c();
    int written = i2c_write_timeout_us(
        i2c,
        (uint8_t)OLED_I2C_ADDR,
        data,
        (int)len,
        false,
        OLED_I2C_TIMEOUT_US
    );
    return written == (int)len;
}

static bool oled_ssd1306_cmds(const uint8_t *cmds, size_t cmd_len) {
    if (cmd_len == 0) return true;
    uint8_t tx[1 + 16];
    if (cmd_len > 16) return false;
    tx[0] = 0x00; // command stream
    memcpy(&tx[1], cmds, cmd_len);
    return oled_i2c_write_timeout(tx, 1 + cmd_len);
}

static void oled_ssd1306_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    uint8_t tx[1 + OLED_I2C_STREAM_CHUNK_BYTES];
    tx[0] = 0x40; // data stream
    while (len > 0) {
        size_t chunk = len;
        if (chunk > (size_t)OLED_I2C_STREAM_CHUNK_BYTES) chunk = (size_t)OLED_I2C_STREAM_CHUNK_BYTES;
        memcpy(&tx[1], data, chunk);
        oled_usb_service();
        (void)oled_i2c_write_timeout(tx, 1 + chunk);
        data += chunk;
        len -= chunk;
    }
}

static void oled_send_buffer_pages(uint8_t first_page, uint8_t page_count) {
    if (page_count == 0) return;
    if (first_page >= 8) return;
    if ((uint8_t)(first_page + page_count) > 8) page_count = (uint8_t)(8 - first_page);

    uint8_t *fb = u8g2_GetBufferPtr(&g_u8g2);
    if (!fb) return;

    uint8_t last_page = (uint8_t)(first_page + page_count - 1);
    const uint8_t set_window[] = {
        0x21, 0x00, (uint8_t)(OLED_WIDTH - 1), // column addr [0..127]
        0x22, first_page, last_page            // page addr [first..last]
    };

    oled_usb_service();
    (void)oled_ssd1306_cmds(set_window, sizeof(set_window));

    const uint8_t *src = fb + ((size_t)first_page * (size_t)OLED_WIDTH);
    size_t remaining = (size_t)page_count * (size_t)OLED_WIDTH;

    oled_ssd1306_data(src, remaining);
}

static void oled_send_buffer_pages_diff(uint8_t first_page, uint8_t page_count) {
    if (page_count == 0) return;
    if (first_page >= 8) return;
    if ((uint8_t)(first_page + page_count) > 8) page_count = (uint8_t)(8 - first_page);
    if (page_count > (uint8_t)OLED_STREAMING_PAGES) page_count = (uint8_t)OLED_STREAMING_PAGES;

    uint8_t *fb = u8g2_GetBufferPtr(&g_u8g2);
    if (!fb) return;

    static uint8_t prev[OLED_WIDTH * OLED_STREAMING_PAGES];
    static bool prev_valid;

    for (uint8_t p = 0; p < page_count; p++) {
        uint8_t page = (uint8_t)(first_page + p);
        const uint8_t *cur = fb + ((size_t)page * (size_t)OLED_WIDTH);
        uint8_t *old = prev + ((size_t)p * (size_t)OLED_WIDTH);

        size_t x = 0;
        while (x < (size_t)OLED_WIDTH) {
            if (prev_valid) {
                while (x < (size_t)OLED_WIDTH && old[x] == cur[x]) x++;
                if (x >= (size_t)OLED_WIDTH) break;
            }

            size_t start = x;
            while (x < (size_t)OLED_WIDTH && (!prev_valid || old[x] != cur[x])) {
                old[x] = cur[x];
                x++;
            }
            size_t end = x - 1;

            uint8_t const set_window[] = {
                0x21, (uint8_t)start, (uint8_t)end, // columns
                0x22, page, page                     // one page
            };
            oled_usb_service();
            (void)oled_ssd1306_cmds(set_window, sizeof(set_window));
            oled_ssd1306_data(cur + start, end - start + 1);
        }
    }

    prev_valid = true;
}

/* Dashed line pattern: segment length and gap in pixels. */
#define DASH_SEGMENT_LEN  4
#define DASH_GAP_LEN     4

static void oled_draw_dashed_hline(u8g2_t *u8g2, uint8_t x0, uint8_t x1, uint8_t y) {
    unsigned int x = x0;
    while (x <= x1) {
        unsigned int seg_end = x + DASH_SEGMENT_LEN;
        if (seg_end > x1) seg_end = x1 + 1;
        u8g2_DrawHLine(u8g2, (uint8_t)x, y, (uint8_t)(seg_end - x));
        x = seg_end + DASH_GAP_LEN;
    }
}

static void oled_draw_dashed_vline(u8g2_t *u8g2, uint8_t x, uint8_t y0, uint8_t y1) {
    unsigned int y = y0;
    while (y <= y1) {
        unsigned int seg_end = y + DASH_SEGMENT_LEN;
        if (seg_end > y1) seg_end = y1 + 1;
        u8g2_DrawVLine(u8g2, x, (uint8_t)y, (uint8_t)(seg_end - y));
        y = seg_end + DASH_GAP_LEN;
    }
}

/* Grid: 3 horizontal and 5 vertical dashed lines over the waveform area. */
#define GRID_TOP_Y    0
#define GRID_BOTTOM_Y (OLED_HEIGHT - 1)
#define GRID_LEFT_X   0
#define GRID_RIGHT_X  (OLED_WIDTH - 1)

static void oled_draw_grid(u8g2_t *u8g2) {
    /* 3 horizontal lines: divide height into 4 bands → y at 1/4, 2/4, 3/4 */
    oled_draw_dashed_hline(u8g2, GRID_LEFT_X, GRID_RIGHT_X, OLED_HEIGHT / 4);      /* 16 */
    oled_draw_dashed_hline(u8g2, GRID_LEFT_X, GRID_RIGHT_X, OLED_HEIGHT / 2);      /* 32 */
    oled_draw_dashed_hline(u8g2, GRID_LEFT_X, GRID_RIGHT_X, (3 * OLED_HEIGHT) / 4); /* 48 */
    /* 5 vertical lines: divide width into 6 bands → x at 1/6 .. 5/6 */
    oled_draw_dashed_vline(u8g2, OLED_WIDTH / 6,                  GRID_TOP_Y, GRID_BOTTOM_Y);
    oled_draw_dashed_vline(u8g2, (2 * OLED_WIDTH) / 6,           GRID_TOP_Y, GRID_BOTTOM_Y);
    oled_draw_dashed_vline(u8g2, (3 * OLED_WIDTH) / 6,           GRID_TOP_Y, GRID_BOTTOM_Y);
    oled_draw_dashed_vline(u8g2, (4 * OLED_WIDTH) / 6,           GRID_TOP_Y, GRID_BOTTOM_Y);
    oled_draw_dashed_vline(u8g2, (5 * OLED_WIDTH) / 6,            GRID_TOP_Y, GRID_BOTTOM_Y);
}

static void oled_draw_boot(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&g_u8g2, 0, 12, "SSD1306 OLED");
    u8g2_DrawStr(&g_u8g2, 0, 26, "u8g2 + Pico");
    u8g2_SendBuffer(&g_u8g2);
}

static const char *oled_crash_reason_txt(crash_reason_t r) {
    switch (r) {
        case CRASH_REASON_HARDFAULT:
            return "HF";
        case CRASH_REASON_PANIC:
            return "PANIC";
        default:
            return "?";
    }
}

static void oled_draw_crash_screen(const crash_info_t *info) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);

    char line[32];
    snprintf(
        line,
        sizeof(line),
        "CRASH %s c%u",
        oled_crash_reason_txt(info->reason),
        (unsigned)info->core_id
    );
    u8g2_DrawStr(&g_u8g2, 0, 10, line);

    snprintf(line, sizeof(line), "pc 0x%08lX", (unsigned long)info->pc);
    u8g2_DrawStr(&g_u8g2, 0, 20, line);

    if (info->reason == CRASH_REASON_PANIC) {
        // For PANIC we store the fmt pointer in lr.
        snprintf(line, sizeof(line), "fmt 0x%08lX", (unsigned long)info->lr);
    } else {
        snprintf(line, sizeof(line), "lr 0x%08lX", (unsigned long)info->lr);
    }
    u8g2_DrawStr(&g_u8g2, 0, 30, line);

    if (info->reason == CRASH_REASON_PANIC) {
        snprintf(line, sizeof(line), "arg0 0x%08lX", (unsigned long)info->arg0);
    } else {
        snprintf(line, sizeof(line), "cfsr 0x%08lX", (unsigned long)info->cfsr);
    }
    u8g2_DrawStr(&g_u8g2, 0, 40, line);

    snprintf(line, sizeof(line), "hfsr 0x%08lX", (unsigned long)info->hfsr);
    u8g2_DrawStr(&g_u8g2, 0, 50, line);

    if (info->cfsr & (1u << 15)) {
        snprintf(line, sizeof(line), "bfar 0x%08lX", (unsigned long)info->bfar);
    } else {
        snprintf(line, sizeof(line), "bfar n/a");
    }
    u8g2_DrawStr(&g_u8g2, 0, 60, line);

    u8g2_SendBuffer(&g_u8g2);
}

static void oled_draw_waveform(const uint8_t y[128]) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);

    char line[32];
    snprintf(line, sizeof(line), "frame %lu", (unsigned long) g_frame_counter);
    u8g2_DrawStr(&g_u8g2, 0, 10, line);

#ifdef ENABLE_USB_AUDIO
    {
        const uint32_t sr = usb_audio_get_sample_rate();
        const char *sr_txt = (sr == 44100u) ? "44.1k" : (sr == 48000u) ? "48k" : "?";

        char usb_line1[32];
        snprintf(
            usb_line1,
            sizeof(usb_line1),
            "USB%s A%u s%d i%lu",
            sr_txt,
            (unsigned)usb_audio_get_last_alt_setting(),
            (int)usb_audio_is_streaming(),
            (unsigned long)usb_audio_get_set_itf_count()
        );
        u8g2_DrawStr(&g_u8g2, 0, 20, usb_line1);

        char usb_line2[32];
        if (usb_audio_is_streaming()) {
            snprintf(
                usb_line2,
                sizeof(usb_line2),
                "r%lu a%lu u%lu",
                (unsigned long)usb_audio_get_ring_fill_frames(),
                (unsigned long)usb_audio_get_last_avail_bytes(),
                (unsigned long)usb_audio_get_underrun_count()
            );
        } else {
            snprintf(
                usb_line2,
                sizeof(usb_line2),
                "rq%02X cs%02X e%02X l%04X",
                (unsigned)usb_audio_get_last_req_bRequest(),
                (unsigned)usb_audio_get_last_req_control_selector(),
                (unsigned)usb_audio_get_last_req_entity_id(),
                (unsigned)usb_audio_get_last_req_wLength()
            );
        }
        u8g2_DrawStr(&g_u8g2, 0, 30, usb_line2);
    }
#endif

    // The grid makes debugging USB text harder (and doesn't help when hunting control-request issues).
    // Keep it disabled for now.

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

static void oled_draw_streaming_status(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x10_tf);

    char line[32];
    snprintf(line, sizeof(line), "frame %lu", (unsigned long) g_frame_counter);
    u8g2_DrawStr(&g_u8g2, 0, 10, line);

#ifdef ENABLE_USB_AUDIO
    const uint32_t sr = usb_audio_get_sample_rate();
    const char *sr_txt = (sr == 44100u) ? "44.1k" : (sr == 48000u) ? "48k" : "?";

    char usb_line1[32];
    snprintf(
        usb_line1,
        sizeof(usb_line1),
        "USB%s A%u s%d i%lu",
        sr_txt,
        (unsigned)usb_audio_get_last_alt_setting(),
        (int)usb_audio_is_streaming(),
        (unsigned long)usb_audio_get_set_itf_count()
    );
    u8g2_DrawStr(&g_u8g2, 0, 20, usb_line1);

    char usb_line2[32];
    snprintf(
        usb_line2,
        sizeof(usb_line2),
        "r%lu a%lu u%lu",
        (unsigned long)usb_audio_get_ring_fill_frames(),
        (unsigned long)usb_audio_get_last_avail_bytes(),
        (unsigned long)usb_audio_get_underrun_count()
    );
    u8g2_DrawStr(&g_u8g2, 0, 30, usb_line2);
#endif

    // Update only the top part of the display while USB audio is streaming.
    // Full-frame I2C transfers can introduce periodic scheduling jitter.
    oled_send_buffer_pages_diff(0, (uint8_t)OLED_STREAMING_PAGES);
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

    // If we rebooted after a fault/panic, show crash info for a few seconds.
    crash_info_t info;
    if (crash_read(&info)) {
        g_crash_info = info;
        crash_clear();
        oled_draw_crash_screen(&g_crash_info);
        g_show_crash_screen = true;
        g_crash_screen_until = make_timeout_time_ms(OLED_CRASH_SCREEN_MS);
    } else {
        g_show_crash_screen = false;
    }

    g_oled_ready = true;
    g_next_frame = make_timeout_time_ms(250);
    g_frame_counter = 0;
    return true;
}

void oled_task(void) {
    if (!g_oled_ready) return;

    absolute_time_t now = get_absolute_time();
    if (g_show_crash_screen) {
        if (absolute_time_diff_us(now, g_crash_screen_until) > 0) {
            // Keep the crash screen visible without refreshing to minimize USB jitter.
            return;
        }
        g_show_crash_screen = false;
        g_next_frame = now;
    }
    if (absolute_time_diff_us(now, g_next_frame) > 0) {
        return; // not yet
    }
    uint32_t fps = (uint32_t)OLED_REFRESH_FPS;
#ifdef ENABLE_USB_AUDIO
    if (usb_audio_is_streaming()) fps = (uint32_t)OLED_REFRESH_FPS_STREAMING;
#endif
    if (fps == 0u) fps = 1u;
    g_next_frame = delayed_by_ms(now, (int32_t)(1000u / fps));
    g_frame_counter++;

#ifdef ENABLE_USB_AUDIO
    if (usb_audio_is_streaming()) {
        oled_draw_streaming_status();
        return;
    }
#endif

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
