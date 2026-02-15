#include "ui/screens/ui_screen_waveform.h"

#include <stddef.h>

#ifdef ENABLE_OLED

#include <stdio.h>

#include "config.h"
#include "dev/oled.h"
#include "multicore_display.h"
#include "ui/ui_manager.h"

static uint32_t g_frame_counter;

/* Dashed line pattern: segment length and gap in pixels. */
#define DASH_SEGMENT_LEN  4
#define DASH_GAP_LEN      4

/* Grid: 3 horizontal and 5 vertical dashed lines over the waveform area. */
#define GRID_TOP_Y    0
#define GRID_BOTTOM_Y (OLED_HEIGHT - 1)
#define GRID_LEFT_X   0
#define GRID_RIGHT_X  (OLED_WIDTH - 1)

static void draw_dashed_hline(u8g2_t *u8g2, uint8_t x0, uint8_t x1, uint8_t y) {
    unsigned int x = x0;
    while (x <= x1) {
        unsigned int seg_end = x + DASH_SEGMENT_LEN;
        if (seg_end > x1) seg_end = x1 + 1;
        u8g2_DrawHLine(u8g2, (uint8_t) x, y, (uint8_t) (seg_end - x));
        x = seg_end + DASH_GAP_LEN;
    }
}

static void draw_dashed_vline(u8g2_t *u8g2, uint8_t x, uint8_t y0, uint8_t y1) {
    unsigned int y = y0;
    while (y <= y1) {
        unsigned int seg_end = y + DASH_SEGMENT_LEN;
        if (seg_end > y1) seg_end = y1 + 1;
        u8g2_DrawVLine(u8g2, x, (uint8_t) y, (uint8_t) (seg_end - y));
        y = seg_end + DASH_GAP_LEN;
    }
}

static void draw_grid(u8g2_t *u8g2) {
    /* 3 horizontal lines: divide height into 4 bands -> y at 1/4, 2/4, 3/4 */
    draw_dashed_hline(u8g2, GRID_LEFT_X, GRID_RIGHT_X, OLED_HEIGHT / 4);
    draw_dashed_hline(u8g2, GRID_LEFT_X, GRID_RIGHT_X, OLED_HEIGHT / 2);
    draw_dashed_hline(u8g2, GRID_LEFT_X, GRID_RIGHT_X, (3 * OLED_HEIGHT) / 4);

    /* 5 vertical lines: divide width into 6 bands -> x at 1/6 .. 5/6 */
    draw_dashed_vline(u8g2, OLED_WIDTH / 6, GRID_TOP_Y, GRID_BOTTOM_Y);
    draw_dashed_vline(u8g2, (2 * OLED_WIDTH) / 6, GRID_TOP_Y, GRID_BOTTOM_Y);
    draw_dashed_vline(u8g2, (3 * OLED_WIDTH) / 6, GRID_TOP_Y, GRID_BOTTOM_Y);
    draw_dashed_vline(u8g2, (4 * OLED_WIDTH) / 6, GRID_TOP_Y, GRID_BOTTOM_Y);
    draw_dashed_vline(u8g2, (5 * OLED_WIDTH) / 6, GRID_TOP_Y, GRID_BOTTOM_Y);
}

static void render_waveform(u8g2_t *u8g2, const uint8_t y[MULTICORE_WAVEFORM_WIDTH]) {
    char line[32];

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

    (void) snprintf(line, sizeof(line), "frame %lu", (unsigned long) g_frame_counter);
    u8g2_DrawStr(u8g2, 0, 10, line);

    draw_grid(u8g2);

    uint8_t prev_y = y[0];
    for (int x = 0; x < MULTICORE_WAVEFORM_WIDTH; x++) {
        uint8_t yy = y[x];
        if (x > 0) {
            u8g2_DrawLine(u8g2, x - 1, prev_y, x, yy);
        } else {
            u8g2_DrawPixel(u8g2, x, yy);
        }
        prev_y = yy;
    }

    oled_backend_flush();
}

static void render_waiting(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 0, 12, "Waiting waveform...");
    u8g2_DrawFrame(u8g2, 0, 20, OLED_WIDTH, OLED_HEIGHT - 20);
    int x = (int) (g_frame_counter % (OLED_WIDTH - 6));
    u8g2_DrawBox(u8g2, x, 24, 6, 6);
    oled_backend_flush();
}

static void waveform_enter(void) {
    g_frame_counter = 0;
}

static void waveform_exit(void) {
    (void) 0;
}

static void waveform_on_action(ui_action_t action) {
    if (action == UI_ACTION_BACK) {
        (void) ui_show_main_menu();
    }
}

static void waveform_render(void) {
    u8g2_t *u8g2 = oled_backend_u8g2();
    if (!u8g2) return;

    g_frame_counter++;

    uint8_t y[MULTICORE_WAVEFORM_WIDTH];
    if (multicore_display_read_latest(y)) {
        render_waveform(u8g2, y);
    } else {
        render_waiting(u8g2);
    }
}

static const ui_screen_t g_waveform_screen = {
    .name = "waveform",
    .on_enter = waveform_enter,
    .on_exit = waveform_exit,
    .on_render = waveform_render,
    .on_action = waveform_on_action
};

const ui_screen_t *ui_screen_waveform_get(void) {
    return &g_waveform_screen;
}

#else

const ui_screen_t *ui_screen_waveform_get(void) {
    return NULL;
}

#endif /* ENABLE_OLED */
