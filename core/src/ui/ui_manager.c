#include "ui/ui_manager.h"

#include "config.h"
#include "pico/time.h"

#ifdef ENABLE_OLED
#include "dev/oled.h"
#include "ui/screens/ui_screen_main_menu.h"
#include "ui/screens/ui_screen_waveform.h"
#ifdef ENABLE_USB_AUDIO
#include "usb/usb_audio.h"
#endif
#if UI_INPUT_ENABLED
#include "hardware/gpio.h"
#endif
#endif

#define UI_BOOT_DELAY_MS 250

static bool g_ui_initialized;
static const ui_screen_t *g_ui_active_screen;
static absolute_time_t g_ui_next_frame;

#if defined(ENABLE_OLED) && defined(ENABLE_USB_AUDIO) && (UI_DISABLE_OLED_WHILE_USB_AUDIO_STREAMING != 0)
static bool g_usb_streaming_prev;
static bool g_usb_streaming_ui_override;
static bool g_usb_streaming_overlay_pending;
static bool g_usb_streaming_safe_chord_armed;
static bool g_usb_streaming_safe_chord_fired;
static absolute_time_t g_usb_streaming_safe_chord_deadline;
enum { UI_USB_STREAMING_SAFE_CHORD_HOLD_MS = 700u };

static bool ui_draw_usb_streaming_overlay_once(void) {
    u8g2_t *u8g2 = oled_backend_u8g2();
    if (!u8g2) {
        return false;
    }
    if (!oled_backend_can_flush()) {
        return false;
    }

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 0, 12, "USB AUDIO ON");
    u8g2_DrawStr(u8g2, 0, 28, "CENTER: UI ON");
    u8g2_DrawStr(u8g2, 0, 40, "Hold L+R: SAFE");
    u8g2_DrawStr(u8g2, 0, 56, "Display isolated");
    oled_backend_flush();
    return true;
}

static bool ui_button_pressed(uint8_t pin) {
#if UI_BTN_ACTIVE_LOW
    return gpio_get(pin) == 0;
#else
    return gpio_get(pin) != 0;
#endif
}

static bool ui_usb_streaming_safe_chord_triggered(void) {
#if UI_INPUT_ENABLED
    bool const both_pressed =
        ui_button_pressed(UI_BTN_LEFT_PIN) &&
        ui_button_pressed(UI_BTN_RIGHT_PIN);

    if (!both_pressed) {
        g_usb_streaming_safe_chord_armed = false;
        g_usb_streaming_safe_chord_fired = false;
        return false;
    }

    if (g_usb_streaming_safe_chord_fired) {
        return false;
    }

    if (!g_usb_streaming_safe_chord_armed) {
        g_usb_streaming_safe_chord_armed = true;
        g_usb_streaming_safe_chord_deadline =
            make_timeout_time_ms(UI_USB_STREAMING_SAFE_CHORD_HOLD_MS);
        return false;
    }

    if (absolute_time_diff_us(get_absolute_time(), g_usb_streaming_safe_chord_deadline) <= 0) {
        g_usb_streaming_safe_chord_fired = true;
        return true;
    }
#endif
    return false;
}
#endif

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

#if defined(ENABLE_USB_AUDIO) && (UI_DISABLE_OLED_WHILE_USB_AUDIO_STREAMING != 0)
    if (usb_audio_is_streaming()) {
        if (!g_usb_streaming_ui_override && action == UI_ACTION_ENTER) {
            g_usb_streaming_ui_override = true;
            g_ui_next_frame = get_absolute_time();
            return;
        }
        if (action == UI_ACTION_BACK) {
            g_usb_streaming_ui_override = false;
            g_usb_streaming_overlay_pending = true;
            return;
        }
        if (!g_usb_streaming_ui_override) {
            return;
        }
    }
#endif

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

#if defined(ENABLE_USB_AUDIO) && (UI_DISABLE_OLED_WHILE_USB_AUDIO_STREAMING != 0)
    g_usb_streaming_prev = false;
    g_usb_streaming_ui_override = false;
    g_usb_streaming_overlay_pending = false;
    g_usb_streaming_safe_chord_armed = false;
    g_usb_streaming_safe_chord_fired = false;
    g_usb_streaming_safe_chord_deadline = get_absolute_time();
#endif

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

#if defined(ENABLE_USB_AUDIO) && (UI_DISABLE_OLED_WHILE_USB_AUDIO_STREAMING != 0)
    bool const streaming = usb_audio_is_streaming();
    if (streaming != g_usb_streaming_prev) {
        g_usb_streaming_prev = streaming;
        if (streaming) {
            g_usb_streaming_ui_override = false;
            g_usb_streaming_overlay_pending = true;
        } else {
            g_usb_streaming_ui_override = false;
            g_usb_streaming_overlay_pending = false;
            g_usb_streaming_safe_chord_armed = false;
            g_usb_streaming_safe_chord_fired = false;
            g_ui_next_frame = get_absolute_time();
        }
    }

    if (streaming && g_usb_streaming_ui_override && ui_usb_streaming_safe_chord_triggered()) {
        g_usb_streaming_ui_override = false;
        g_usb_streaming_overlay_pending = true;
        return;
    }

    if (streaming && !g_usb_streaming_ui_override) {
        if (g_usb_streaming_overlay_pending && ui_draw_usb_streaming_overlay_once()) {
            g_usb_streaming_overlay_pending = false;
        }
        return;
    }
#endif

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, g_ui_next_frame) > 0) {
        return;
    }

    // Backpressure: skip expensive draw while previous OLED transfer is still in flight.
    if (!oled_backend_can_flush()) {
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
