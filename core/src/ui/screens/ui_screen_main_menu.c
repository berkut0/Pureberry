#include "ui/screens/ui_screen_main_menu.h"
#include <stddef.h>

#ifdef ENABLE_OLED

#include "pico/time.h"

#include "dev/oled.h"
#include "ui/mui/ui_mui_forms.h"
#include "ui/mui/ui_mui_runtime.h"
#include "ui/ui_manager.h"
#ifdef ENABLE_USB_AUDIO
#include "usb/usb_audio.h"
#endif

static bool g_menu_ready;
static absolute_time_t g_menu_next_render;
enum { UI_MENU_USB_DIAG_RENDER_PERIOD_MS = 250u };

static bool menu_usb_diag_throttle_active(void) {
#ifdef ENABLE_USB_AUDIO
    if (!usb_audio_is_streaming()) {
        return false;
    }
    return ui_mui_runtime_current_form() == (int) UI_MUI_FORM_INPUTS;
#else
    return false;
#endif
}

static bool menu_should_render_now(void) {
    if (!menu_usb_diag_throttle_active()) {
        return true;
    }

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, g_menu_next_render) > 0) {
        return false;
    }

    g_menu_next_render = delayed_by_ms(now, UI_MENU_USB_DIAG_RENDER_PERIOD_MS);
    return true;
}

static void menu_enter(void) {
    u8g2_t *u8g2 = oled_backend_u8g2();
    if (!u8g2) {
        g_menu_ready = false;
        return;
    }

    g_menu_ready = ui_mui_runtime_init(u8g2);
    g_menu_next_render = get_absolute_time();
}

static void menu_exit(void) {
    g_menu_ready = false;
}

static void menu_on_action(ui_action_t action) {
    if (!g_menu_ready) {
        return;
    }

    (void) ui_mui_runtime_handle_action(action);
    g_menu_next_render = get_absolute_time();

    if (ui_mui_runtime_take_exit_to_waveform()) {
        (void) ui_show_waveform();
    }
}

static void menu_on_render(void) {
    if (!g_menu_ready) {
        return;
    }

    if (!menu_should_render_now()) {
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
