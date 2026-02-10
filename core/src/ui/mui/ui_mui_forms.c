#include "ui/mui/ui_mui_forms.h"

#ifdef ENABLE_OLED

#include <stdio.h>
#include <string.h>

#include "mui_u8g2.h"
#include "u8g2.h"
#include "ui/system/ui_system_stats.h"

#include "ui/mui/ui_mui_runtime.h"

typedef enum {
    UI_MUI_SYSTEM_PAGE_MAIN = 0,
    UI_MUI_SYSTEM_PAGE_ADV = 1,
} ui_mui_system_page_t;

static ui_mui_system_page_t g_system_page;
static ui_system_stats_snapshot_t g_system_snapshot;
static ui_mui_runtime_stats_t g_system_mui_runtime_stats;

static uint8_t ui_mui_style_default(mui_t *ui, uint8_t msg) {
    if (msg != MUIF_MSG_DRAW) {
        return 0;
    }

    u8g2_t *u8g2 = mui_get_U8g2(ui);
    if (!u8g2) {
        return 0;
    }

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    return 0;
}

static uint8_t ui_mui_hline(mui_t *ui, uint8_t msg) {
    if (msg != MUIF_MSG_DRAW) {
        return 0;
    }

    u8g2_t *u8g2 = mui_get_U8g2(ui);
    if (!u8g2) {
        return 0;
    }

    u8g2_DrawHLine(u8g2, 0, mui_get_y(ui), u8g2_GetDisplayWidth(u8g2));
    return 0;
}

static void ui_mui_format_compact_u32(char *out, size_t out_len, uint32_t value) {
    if (!out || out_len == 0u) {
        return;
    }
    if (value < 1000u) {
        (void) snprintf(out, out_len, "%lu", (unsigned long) value);
        return;
    }
    if (value < 10000u) {
        (void) snprintf(out, out_len, "%lu.%luk",
                        (unsigned long) (value / 1000u),
                        (unsigned long) ((value % 1000u) / 100u));
        return;
    }
    if (value < 1000000u) {
        (void) snprintf(out, out_len, "%luk", (unsigned long) (value / 1000u));
        return;
    }
    if (value < 10000000u) {
        (void) snprintf(out, out_len, "%lu.%luM",
                        (unsigned long) (value / 1000000u),
                        (unsigned long) ((value % 1000000u) / 100000u));
        return;
    }
    (void) snprintf(out, out_len, "%luM", (unsigned long) (value / 1000000u));
}

static void ui_mui_system_make_main_line(uint8_t line, char *out, size_t out_len) {
    if (!out || out_len == 0u) {
        return;
    }

    switch (line) {
        case 0: {
            const uint32_t dsp_int = g_system_snapshot.core1_dsp_avg_permille / 10u;
            const uint32_t dsp_frac = g_system_snapshot.core1_dsp_avg_permille % 10u;
            (void) snprintf(out, out_len, "C1 %lu.%lu%% CPU %lu",
                            (unsigned long) dsp_int,
                            (unsigned long) dsp_frac,
                            (unsigned long) g_system_snapshot.cpu_mhz);
            break;
        }
        case 1:
            (void) snprintf(out, out_len, "RAM used %luK",
                            (unsigned long) (g_system_snapshot.ram_heap_used_bytes / 1024u));
            break;
        case 2:
            (void) snprintf(out, out_len, "RAM free %luK",
                            (unsigned long) (g_system_snapshot.ram_heap_free_bytes / 1024u));
            break;
        case 3:
            (void) snprintf(out, out_len, "Uptime %lus",
                            (unsigned long) g_system_snapshot.uptime_s);
            break;
        default:
            out[0] = '\0';
            break;
    }
}

static void ui_mui_system_make_adv_line(uint8_t line, char *out, size_t out_len) {
    if (!out || out_len == 0u) {
        return;
    }

    char a[8];
    char b[8];
    char c[8];
    char d[8];

    switch (line) {
        case 0:
            (void) snprintf(out, out_len, "HV %luK IN %u",
                            (unsigned long) (g_system_snapshot.heavy_ctx_bytes / 1024u),
                            (unsigned int) g_system_snapshot.heavy_in_count);
            break;
        case 1:
            ui_mui_format_compact_u32(a, sizeof(a), g_system_snapshot.transport.ctrl_queue_push_ok);
            ui_mui_format_compact_u32(b, sizeof(b), g_system_snapshot.transport.ctrl_queue_push_drop);
            (void) snprintf(out, out_len, "Q ok%s dr%s", a, b);
            break;
        case 2:
            ui_mui_format_compact_u32(a, sizeof(a), g_system_snapshot.transport.ctrl_queue_push_drop_usb_midi);
            ui_mui_format_compact_u32(b, sizeof(b), g_system_snapshot.transport.ctrl_queue_push_drop_adc_pots);
            ui_mui_format_compact_u32(c, sizeof(c), g_system_snapshot.transport.ctrl_queue_push_drop_mpr121);
            (void) snprintf(out, out_len, "D U%s A%s M%s", a, b, c);
            break;
        case 3:
            ui_mui_format_compact_u32(a, sizeof(a), g_system_snapshot.input.actions_emitted);
            ui_mui_format_compact_u32(b, sizeof(b), g_system_mui_runtime_stats.actions_forwarded);
            ui_mui_format_compact_u32(c, sizeof(c), g_system_mui_runtime_stats.draw_calls);
            ui_mui_format_compact_u32(d, sizeof(d), g_system_snapshot.input.actions_process_budget_hit);
            (void) snprintf(out, out_len, "UI e%s f%s d%s b%s", a, b, c, d);
            break;
        default:
            out[0] = '\0';
            break;
    }
}

static uint8_t ui_mui_system_title(mui_t *ui, uint8_t msg) {
    if (msg != MUIF_MSG_DRAW) {
        return 0;
    }

    u8g2_t *u8g2 = mui_get_U8g2(ui);
    if (!u8g2) {
        return 0;
    }

    const char *title = (g_system_page == UI_MUI_SYSTEM_PAGE_MAIN) ? "System Main" : "System Adv";
    u8g2_DrawUTF8(u8g2, mui_get_x(ui), mui_get_y(ui), title);
    return 0;
}

static uint8_t ui_mui_system_line(mui_t *ui, uint8_t msg) {
    if (msg != MUIF_MSG_DRAW) {
        return 0;
    }

    u8g2_t *u8g2 = mui_get_U8g2(ui);
    if (!u8g2) {
        return 0;
    }

    char line[28];
    line[0] = '\0';

    const uint8_t idx = (uint8_t) mui_get_arg(ui);
    if (g_system_page == UI_MUI_SYSTEM_PAGE_MAIN) {
        ui_mui_system_make_main_line(idx, line, sizeof(line));
    } else {
        ui_mui_system_make_adv_line(idx, line, sizeof(line));
    }

    if (line[0] != '\0') {
        u8g2_DrawUTF8(u8g2, mui_get_x(ui), mui_get_y(ui), line);
    }
    return 0;
}

static muif_t g_muif_list[] MUI_PROGMEM = {
    MUIF_STYLE(0, ui_mui_style_default),
    MUIF_RO("HR", ui_mui_hline),
    MUIF_RO("ST", ui_mui_system_title),
    MUIF_RO("SD", ui_mui_system_line),
    MUIF_U8G2_LABEL(),
    MUIF_GOTO(mui_u8g2_btn_goto_w1_fi),
};

static fds_t g_fds[] MUI_PROGMEM =
    MUI_FORM(1)
    MUI_STYLE(0)
    MUI_LABEL(2, 10, "Main Menu")
    MUI_XY("HR", 0, 12)
    MUI_GOTO(4, 24, 9, "Waveform")
    MUI_GOTO(4, 38, 2, "Inputs")
    MUI_GOTO(4, 52, 3, "System")

    MUI_FORM(2)
    MUI_STYLE(0)
    MUI_LABEL(2, 10, "Inputs")
    MUI_XY("HR", 0, 12)
    MUI_LABEL(2, 32, "Not implemented yet")
    MUI_GOTO(4, 52, 1, "Back")

    MUI_FORM(3)
    MUI_STYLE(0)
    MUI_XY("ST", 2, 10)
    MUI_XY("HR", 0, 12)
    MUI_XYA("SD", 2, 24, 0)
    MUI_XYA("SD", 2, 34, 1)
    MUI_XYA("SD", 2, 44, 2)
    MUI_XYA("SD", 2, 54, 3)

    MUI_FORM(9)
    MUI_STYLE(0)
    MUI_LABEL(2, 32, "Opening waveform...")
;

fds_t *ui_mui_forms_fds(void) {
    return g_fds;
}

muif_t *ui_mui_forms_muif(size_t *out_count) {
    if (out_count) {
        *out_count = sizeof(g_muif_list) / sizeof(g_muif_list[0]);
    }
    return g_muif_list;
}

uint8_t ui_mui_forms_root_form_id(void) {
    return (uint8_t) UI_MUI_FORM_ROOT;
}

void ui_mui_forms_on_before_draw(int form_id) {
    if (form_id != (int) UI_MUI_FORM_SYSTEM) {
        return;
    }

    ui_system_stats_poll();
    if (!ui_system_stats_get_snapshot(&g_system_snapshot)) {
        memset(&g_system_snapshot, 0, sizeof(g_system_snapshot));
    }
    ui_mui_runtime_get_stats(&g_system_mui_runtime_stats);
}

void ui_mui_forms_on_form_enter(int form_id) {
    if (form_id == (int) UI_MUI_FORM_SYSTEM) {
        g_system_page = UI_MUI_SYSTEM_PAGE_MAIN;
        ui_mui_forms_on_before_draw(form_id);
    }
}

bool ui_mui_forms_try_handle_action(int form_id, ui_action_t action) {
    if (form_id != (int) UI_MUI_FORM_SYSTEM) {
        return false;
    }

    if (action == UI_ACTION_ENTER) {
        g_system_page = (g_system_page == UI_MUI_SYSTEM_PAGE_MAIN)
            ? UI_MUI_SYSTEM_PAGE_ADV
            : UI_MUI_SYSTEM_PAGE_MAIN;
        return true;
    }

    return false;
}

#endif /* ENABLE_OLED */
