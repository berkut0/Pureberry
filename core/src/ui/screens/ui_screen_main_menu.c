#include "ui/screens/ui_screen_main_menu.h"

#ifdef ENABLE_OLED

#include "pico/time.h"

#include "config.h"
#include "dev/oled.h"
#include "ui/ui_manager.h"

typedef enum {
    MENU_ITEM_WAVEFORM = 0,
    MENU_ITEM_INPUTS,
    MENU_ITEM_SYSTEM,
    MENU_ITEM_COUNT
} ui_menu_item_t;

static const char *g_menu_items[MENU_ITEM_COUNT] = {
    "Waveform",
    "Inputs",
    "System",
};

static uint8_t g_selected_item;
static absolute_time_t g_hint_until;
static const char *g_hint_text;

static void menu_set_hint(const char *text, uint32_t duration_ms) {
    g_hint_text = text;
    g_hint_until = make_timeout_time_ms(duration_ms);
}

static bool menu_hint_active(absolute_time_t now) {
    return absolute_time_diff_us(now, g_hint_until) > 0;
}

static void menu_render(u8g2_t *u8g2) {
    absolute_time_t now = get_absolute_time();

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

    u8g2_DrawStr(u8g2, 0, 10, "Main Menu");
    u8g2_DrawHLine(u8g2, 0, 12, OLED_WIDTH);

    for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
        uint8_t y = (uint8_t) (24 + i * 12);
        if (i == g_selected_item) {
            u8g2_DrawStr(u8g2, 0, y, ">");
        }
        u8g2_DrawStr(u8g2, 10, y, g_menu_items[i]);
    }

    if (menu_hint_active(now) && g_hint_text) {
        u8g2_DrawStr(u8g2, 0, 62, g_hint_text);
    } else {
        u8g2_DrawStr(u8g2, 0, 62, "Hold C: waveform");
    }

    oled_backend_flush();
}

static void menu_enter(void) {
    g_selected_item = MENU_ITEM_WAVEFORM;
    g_hint_text = NULL;
    g_hint_until = get_absolute_time();
}

static void menu_exit(void) {
    (void) 0;
}

static void menu_select_current(void) {
    switch ((ui_menu_item_t) g_selected_item) {
        case MENU_ITEM_WAVEFORM:
            (void) ui_show_waveform();
            break;
        case MENU_ITEM_INPUTS:
        case MENU_ITEM_SYSTEM:
        default:
            menu_set_hint("Not implemented", 900);
            break;
    }
}

static void menu_on_action(ui_action_t action) {
    switch (action) {
        case UI_ACTION_NAV_PREV:
            g_selected_item = (uint8_t) ((g_selected_item + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT);
            break;
        case UI_ACTION_NAV_NEXT:
            g_selected_item = (uint8_t) ((g_selected_item + 1) % MENU_ITEM_COUNT);
            break;
        case UI_ACTION_ENTER:
            menu_select_current();
            break;
        case UI_ACTION_BACK:
            (void) ui_show_waveform();
            break;
        default:
            break;
    }
}

static void menu_on_render(void) {
    u8g2_t *u8g2 = oled_backend_u8g2();
    if (!u8g2) return;
    menu_render(u8g2);
}

static const ui_screen_t g_main_menu_screen = {
    .name = "main_menu",
    .on_enter = menu_enter,
    .on_exit = menu_exit,
    .on_render = menu_on_render,
    .on_action = menu_on_action,
};

const ui_screen_t *ui_screen_main_menu_get(void) {
    return &g_main_menu_screen;
}

#else

const ui_screen_t *ui_screen_main_menu_get(void) {
    return NULL;
}

#endif /* ENABLE_OLED */
