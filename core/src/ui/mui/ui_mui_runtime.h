#ifndef UI_MUI_RUNTIME_H
#define UI_MUI_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_screen.h"

#ifdef ENABLE_OLED
#include "u8g2.h"
#else
typedef struct u8g2_struct u8g2_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t draw_calls;
    uint32_t actions_forwarded;
    uint32_t actions_ignored;
} ui_mui_runtime_stats_t;

bool ui_mui_runtime_init(u8g2_t *u8g2);
bool ui_mui_runtime_enter_root(void);
void ui_mui_runtime_draw(void);
bool ui_mui_runtime_handle_action(ui_action_t action);
bool ui_mui_runtime_take_exit_to_waveform(void);
int ui_mui_runtime_current_form(void);
void ui_mui_runtime_get_stats(ui_mui_runtime_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* UI_MUI_RUNTIME_H */
