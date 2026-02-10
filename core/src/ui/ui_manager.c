#include "ui/ui_manager.h"

#include "config.h"
#include "pico/time.h"

#ifdef ENABLE_OLED
#include "dev/oled.h"
#include "ui/screens/ui_screen_main_menu.h"
#include "ui/screens/ui_screen_waveform.h"
#endif

#define UI_BOOT_DELAY_MS 250

static bool g_ui_initialized;
static const ui_screen_t *g_ui_active_screen;
static absolute_time_t g_ui_next_frame;

bool ui_set_screen(const ui_screen_t *screen) {
    if (!g_ui_initialized || !screen) return false;
    if (g_ui_active_screen == screen) return true;

    if (g_ui_active_screen && g_ui_active_screen->on_exit) {
        g_ui_active_screen->on_exit();
    }

    g_ui_active_screen = screen;
    if (g_ui_active_screen->on_enter) {
        g_ui_active_screen->on_enter();
    }
    return true;
}

void ui_post_action(ui_action_t action) {
    if (!g_ui_initialized || !g_ui_active_screen) return;
    if (!g_ui_active_screen->on_action) return;
    g_ui_active_screen->on_action(action);
}

bool ui_show_waveform(void) {
#ifdef ENABLE_OLED
    const ui_screen_t *screen = ui_screen_waveform_get();
    if (!screen) return false;
    return ui_set_screen(screen);
#else
    return false;
#endif
}

bool ui_show_main_menu(void) {
#ifdef ENABLE_OLED
    const ui_screen_t *screen = ui_screen_main_menu_get();
    if (!screen) return false;
    return ui_set_screen(screen);
#else
    return false;
#endif
}

bool ui_init(void) {
#ifdef ENABLE_OLED
    if (!oled_backend_init()) {
        g_ui_initialized = false;
        g_ui_active_screen = NULL;
        return false;
    }

    g_ui_next_frame = make_timeout_time_ms(UI_BOOT_DELAY_MS);
    g_ui_initialized = true;
    g_ui_active_screen = NULL;

    (void) ui_show_waveform();
    return true;
#else
    g_ui_initialized = false;
    g_ui_active_screen = NULL;
    return true;
#endif
}

void ui_task(void) {
#ifdef ENABLE_OLED
    if (!g_ui_initialized || !g_ui_active_screen) return;

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, g_ui_next_frame) > 0) {
        return;
    }
    g_ui_next_frame = delayed_by_ms(now, 1000 / OLED_REFRESH_FPS);

    if (g_ui_active_screen->on_render) {
        g_ui_active_screen->on_render();
    }
#else
    (void) g_ui_initialized;
    (void) g_ui_active_screen;
#endif
}
