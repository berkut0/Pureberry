#ifndef UI_ACTION_MAP_H
#define UI_ACTION_MAP_H

#include <stdbool.h>

#include "input/ui_input_backend.h"
#include "ui/ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ui_action_map_map_event(const ui_input_raw_event_t *event, ui_action_t *out_action);

#ifdef __cplusplus
}
#endif

#endif /* UI_ACTION_MAP_H */
