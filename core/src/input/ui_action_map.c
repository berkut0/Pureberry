#include "input/ui_action_map.h"

bool ui_action_map_map_event(const ui_input_raw_event_t *event, ui_action_t *out_action) {
    if (!event || !out_action) {
        return false;
    }

    *out_action = UI_ACTION_NONE;

    switch (event->button_id) {
        case UI_INPUT_BTN_LEFT:
            if (event->type == UI_INPUT_EVENT_PRESS_UP || event->type == UI_INPUT_EVENT_SINGLE_CLICK) {
                *out_action = UI_ACTION_NAV_PREV;
                return true;
            }
            if (event->type == UI_INPUT_EVENT_LONG_PRESS_HOLD) {
                *out_action = UI_ACTION_VALUE_DEC;
                return true;
            }
            break;

        case UI_INPUT_BTN_CENTER:
            if (event->type == UI_INPUT_EVENT_PRESS_UP || event->type == UI_INPUT_EVENT_SINGLE_CLICK) {
                *out_action = UI_ACTION_ENTER;
                return true;
            }
            if (event->type == UI_INPUT_EVENT_LONG_PRESS_START) {
                *out_action = UI_ACTION_BACK;
                return true;
            }
            break;

        case UI_INPUT_BTN_RIGHT:
            if (event->type == UI_INPUT_EVENT_PRESS_UP || event->type == UI_INPUT_EVENT_SINGLE_CLICK) {
                *out_action = UI_ACTION_NAV_NEXT;
                return true;
            }
            if (event->type == UI_INPUT_EVENT_LONG_PRESS_HOLD) {
                *out_action = UI_ACTION_VALUE_INC;
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}
