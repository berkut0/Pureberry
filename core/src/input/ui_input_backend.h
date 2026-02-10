#ifndef UI_INPUT_BACKEND_H
#define UI_INPUT_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_INPUT_BTN_LEFT = 0,
    UI_INPUT_BTN_CENTER,
    UI_INPUT_BTN_RIGHT,
    UI_INPUT_BTN_COUNT
} ui_input_button_id_t;

typedef enum {
    UI_INPUT_EVENT_PRESS_DOWN = 0,
    UI_INPUT_EVENT_PRESS_UP,
    UI_INPUT_EVENT_PRESS_REPEAT,
    UI_INPUT_EVENT_SINGLE_CLICK,
    UI_INPUT_EVENT_DOUBLE_CLICK,
    UI_INPUT_EVENT_LONG_PRESS_START,
    UI_INPUT_EVENT_LONG_PRESS_HOLD
} ui_input_event_type_t;

typedef struct {
    ui_input_button_id_t button_id;
    ui_input_event_type_t type;
    uint8_t repeat_count;
} ui_input_raw_event_t;

typedef struct {
    uint32_t tick_calls;
    uint32_t events_generated;
    uint32_t events_dropped;
    uint32_t events_popped;
} ui_input_backend_counters_t;

bool ui_input_backend_init(void);
void ui_input_backend_task(void);
bool ui_input_backend_pop(ui_input_raw_event_t *out_event);
void ui_input_backend_get_counters(ui_input_backend_counters_t *out_counters);

#ifdef __cplusplus
}
#endif

#endif /* UI_INPUT_BACKEND_H */
