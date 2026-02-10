#ifndef UI_MUI_FORMS_H
#define UI_MUI_FORMS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_screen.h"

#define UI_MUI_FORM_ROOT   1u
#define UI_MUI_FORM_INPUTS 2u
#define UI_MUI_FORM_SYSTEM 3u
#define UI_MUI_FORM_EXIT_WAVEFORM 9u

#ifdef ENABLE_OLED
#include "mui.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_OLED
fds_t *ui_mui_forms_fds(void);
muif_t *ui_mui_forms_muif(size_t *out_count);
uint8_t ui_mui_forms_root_form_id(void);
void ui_mui_forms_on_before_draw(int form_id);
void ui_mui_forms_on_form_enter(int form_id);
bool ui_mui_forms_try_handle_action(int form_id, ui_action_t action);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UI_MUI_FORMS_H */
