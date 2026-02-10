#include "ui/mui/ui_mui_forms.h"

#ifdef ENABLE_OLED

#include "mui_u8g2.h"
#include "u8g2.h"

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

static muif_t g_muif_list[] MUI_PROGMEM = {
    MUIF_STYLE(0, ui_mui_style_default),
    MUIF_RO("HR", ui_mui_hline),
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
    MUI_LABEL(2, 10, "System")
    MUI_XY("HR", 0, 12)
    MUI_LABEL(2, 32, "Not implemented yet")
    MUI_GOTO(4, 52, 1, "Back")

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

#endif /* ENABLE_OLED */
