#include "ui/screens/ui_screen_main_menu.h"
#include <stddef.h>

#ifdef ENABLE_OLED

#include "dev/oled.h"
#include "ui/mui/ui_mui_runtime.h"
#include "ui/ui_manager.h"

static bool g_menu_ready;

static void menu_enter(void) {
    u8g2_t *u8g2 = oled_backend_u8g2();
    if (!u8g2) {
        g_menu_ready = false;
        return;
    }

    g_menu_ready = ui_mui_runtime_init(u8g2);
}

static void menu_exit(void) {
    g_menu_ready = false;
}

static void menu_on_action(ui_action_t action) {
    if (!g_menu_ready) {
        return;
    }

    (void) ui_mui_runtime_handle_action(action);

    if (ui_mui_runtime_take_exit_to_waveform()) {
        (void) ui_show_waveform();
    }
}

static void menu_on_render(void) {
    if (!g_menu_ready) {
        return;
    }

    ui_mui_runtime_draw();

    if (ui_mui_runtime_take_exit_to_waveform()) {
        (void) ui_show_waveform();
        return;
    }

    oled_backend_flush();
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
