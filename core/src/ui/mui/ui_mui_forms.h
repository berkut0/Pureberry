#ifndef UI_MUI_FORMS_H
#define UI_MUI_FORMS_H

#include <stddef.h>
#include <stdint.h>

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
#endif

#ifdef __cplusplus
}
#endif

#endif /* UI_MUI_FORMS_H */
