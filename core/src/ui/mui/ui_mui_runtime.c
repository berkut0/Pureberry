#include "ui/mui/ui_mui_runtime.h"

#include <string.h>

#ifdef ENABLE_OLED

#include "mui.h"
#include "mui_u8g2.h"

#include "ui/mui/ui_mui_forms.h"

static mui_t g_mui;
static bool g_runtime_ready;
static bool g_exit_to_waveform;
static ui_mui_runtime_stats_t g_stats;
static int g_last_form_id = -1;

static void ui_mui_runtime_process_special_forms(void) {
    if (!g_runtime_ready) {
        return;
    }

    const int form_id = mui_GetCurrentFormId(&g_mui);
    if (form_id == (int) UI_MUI_FORM_EXIT_WAVEFORM) {
        mui_LeaveForm(&g_mui);
        g_exit_to_waveform = true;
    }
}

static void ui_mui_runtime_update_exit_flag(void) {
    if (!g_runtime_ready) {
        return;
    }
    if (!mui_IsFormActive(&g_mui)) {
        g_exit_to_waveform = true;
    }
}

static void ui_mui_runtime_handle_form_change(void) {
    if (!g_runtime_ready) {
        return;
    }

    const int form_id = mui_GetCurrentFormId(&g_mui);
    if (form_id == g_last_form_id) {
        return;
    }

    g_last_form_id = form_id;
    ui_mui_forms_on_form_enter(form_id);
}

bool ui_mui_runtime_init(u8g2_t *u8g2) {
    if (!u8g2) {
        g_runtime_ready = false;
        return false;
    }

    size_t muif_count = 0;
    muif_t *muif = ui_mui_forms_muif(&muif_count);
    fds_t *fds = ui_mui_forms_fds();
    if (!muif || !fds || muif_count == 0) {
        g_runtime_ready = false;
        return false;
    }

    mui_Init(&g_mui, u8g2, fds, muif, muif_count);
    g_runtime_ready = true;
    g_exit_to_waveform = false;
    g_last_form_id = -1;
    memset(&g_stats, 0, sizeof(g_stats));
    return ui_mui_runtime_enter_root();
}

bool ui_mui_runtime_enter_root(void) {
    if (!g_runtime_ready) {
        return false;
    }

    g_exit_to_waveform = false;
    g_last_form_id = -1;
    const bool ok = mui_GotoForm(&g_mui, ui_mui_forms_root_form_id(), 0) != 0;
    if (ok) {
        ui_mui_runtime_handle_form_change();
    }
    return ok;
}

void ui_mui_runtime_draw(void) {
    if (!g_runtime_ready) {
        return;
    }

    ui_mui_runtime_process_special_forms();
    if (g_exit_to_waveform) {
        return;
    }

    if (!mui_IsFormActive(&g_mui)) {
        g_exit_to_waveform = true;
        return;
    }

    ui_mui_runtime_handle_form_change();
    ui_mui_forms_on_before_draw(mui_GetCurrentFormId(&g_mui));

    u8g2_t *u8g2 = mui_get_U8g2(&g_mui);
    if (!u8g2) {
        return;
    }

    u8g2_ClearBuffer(u8g2);
    mui_Draw(&g_mui);
    ui_mui_runtime_process_special_forms();
    g_stats.draw_calls++;
}

bool ui_mui_runtime_handle_action(ui_action_t action) {
    if (!g_runtime_ready) {
        return false;
    }

    const uint8_t root_form_id = ui_mui_forms_root_form_id();
    const int form_id = mui_GetCurrentFormId(&g_mui);
    bool handled = true;

    if (form_id >= 0 && ui_mui_forms_try_handle_action(form_id, action)) {
        g_stats.actions_forwarded++;
        ui_mui_runtime_process_special_forms();
        ui_mui_runtime_update_exit_flag();
        ui_mui_runtime_handle_form_change();
        return true;
    }

    switch (action) {
        case UI_ACTION_NAV_NEXT:
            mui_NextField(&g_mui);
            break;

        case UI_ACTION_NAV_PREV:
            mui_PrevField(&g_mui);
            break;

        case UI_ACTION_ENTER:
            mui_SendSelect(&g_mui);
            break;

        case UI_ACTION_BACK:
            if (form_id < 0 || form_id == (int) root_form_id) {
                mui_LeaveForm(&g_mui);
                g_exit_to_waveform = true;
            } else {
                (void) mui_GotoForm(&g_mui, root_form_id, 0);
            }
            break;

        case UI_ACTION_VALUE_INC:
            mui_SendValueIncrement(&g_mui);
            break;

        case UI_ACTION_VALUE_DEC:
            mui_SendValueDecrement(&g_mui);
            break;

        case UI_ACTION_NONE:
        default:
            handled = false;
            break;
    }

    if (handled) {
        g_stats.actions_forwarded++;
        ui_mui_runtime_process_special_forms();
        ui_mui_runtime_update_exit_flag();
        ui_mui_runtime_handle_form_change();
    } else {
        g_stats.actions_ignored++;
    }

    return handled;
}

bool ui_mui_runtime_take_exit_to_waveform(void) {
    const bool requested = g_exit_to_waveform;
    g_exit_to_waveform = false;
    return requested;
}

int ui_mui_runtime_current_form(void) {
    if (!g_runtime_ready) {
        return -1;
    }
    return mui_GetCurrentFormId(&g_mui);
}

void ui_mui_runtime_get_stats(ui_mui_runtime_stats_t *out_stats) {
    if (!out_stats) {
        return;
    }
    *out_stats = g_stats;
}

#else

bool ui_mui_runtime_init(u8g2_t *u8g2) {
    (void) u8g2;
    return false;
}

bool ui_mui_runtime_enter_root(void) {
    return false;
}

void ui_mui_runtime_draw(void) {
    (void) 0;
}

bool ui_mui_runtime_handle_action(ui_action_t action) {
    (void) action;
    return false;
}

bool ui_mui_runtime_take_exit_to_waveform(void) {
    return false;
}

int ui_mui_runtime_current_form(void) {
    return -1;
}

void ui_mui_runtime_get_stats(ui_mui_runtime_stats_t *out_stats) {
    if (!out_stats) {
        return;
    }
    memset(out_stats, 0, sizeof(*out_stats));
}

#endif /* ENABLE_OLED */
