#include "input/ui_input_backend.h"

#include <string.h>

#include "config.h"

#if UI_INPUT_ENABLED

#include "hardware/gpio.h"
#include "multi_button.h"
#include "pico/time.h"

#ifndef UI_INPUT_EVENT_QUEUE_SIZE
#define UI_INPUT_EVENT_QUEUE_SIZE 16u
#endif

#ifndef UI_INPUT_MAX_TICKS_PER_TASK
#define UI_INPUT_MAX_TICKS_PER_TASK 4u
#endif

#if UI_INPUT_TICK_MS == 0
#error "UI_INPUT_TICK_MS must be > 0"
#endif

static const uint8_t g_button_pins[UI_INPUT_BTN_COUNT] = {
    UI_BTN_LEFT_PIN,
    UI_BTN_CENTER_PIN,
    UI_BTN_RIGHT_PIN,
};

static Button g_buttons[UI_INPUT_BTN_COUNT];
static ui_input_raw_event_t g_events[UI_INPUT_EVENT_QUEUE_SIZE];
static uint32_t g_queue_head;
static uint32_t g_queue_count;
static ui_input_backend_counters_t g_counters;
static absolute_time_t g_next_tick;
static bool g_ready;

static bool queue_push(ui_input_button_id_t button_id, ui_input_event_type_t type, uint8_t repeat_count) {
    g_counters.events_generated++;

    if (g_queue_count >= UI_INPUT_EVENT_QUEUE_SIZE) {
        g_counters.events_dropped++;
        return false;
    }

    uint32_t write_idx = (g_queue_head + g_queue_count) % UI_INPUT_EVENT_QUEUE_SIZE;
    g_events[write_idx].button_id = button_id;
    g_events[write_idx].type = type;
    g_events[write_idx].repeat_count = repeat_count;
    g_queue_count++;
    return true;
}

static uint8_t gpio_read_button_level(uint8_t button_id) {
    if (button_id >= UI_INPUT_BTN_COUNT) {
#if UI_BTN_ACTIVE_LOW
        return 1u;
#else
        return 0u;
#endif
    }

    return gpio_get(g_button_pins[button_id]) ? 1u : 0u;
}

static void push_event_from_button(Button *button, ui_input_event_type_t type) {
    if (!button || button->button_id >= UI_INPUT_BTN_COUNT) {
        return;
    }
    (void) queue_push((ui_input_button_id_t) button->button_id, type, button_get_repeat_count(button));
}

static void on_press_down(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_PRESS_DOWN);
}

static void on_press_up(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_PRESS_UP);
}

static void on_press_repeat(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_PRESS_REPEAT);
}

static void on_single_click(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_SINGLE_CLICK);
}

static void on_double_click(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_DOUBLE_CLICK);
}

static void on_long_press_start(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_LONG_PRESS_START);
}

static void on_long_press_hold(Button *button) {
    push_event_from_button(button, UI_INPUT_EVENT_LONG_PRESS_HOLD);
}

bool ui_input_backend_init(void) {
    memset(&g_counters, 0, sizeof(g_counters));
    memset(g_events, 0, sizeof(g_events));
    memset(g_buttons, 0, sizeof(g_buttons));
    g_queue_head = 0;
    g_queue_count = 0;
    g_ready = false;

    for (uint32_t i = 0; i < UI_INPUT_BTN_COUNT; i++) {
        gpio_init(g_button_pins[i]);
        gpio_set_dir(g_button_pins[i], GPIO_IN);
#if UI_BTN_ACTIVE_LOW
        gpio_pull_up(g_button_pins[i]);
#else
        gpio_pull_down(g_button_pins[i]);
#endif

        button_init(
            &g_buttons[i],
            gpio_read_button_level,
#if UI_BTN_ACTIVE_LOW
            0u,
#else
            1u,
#endif
            (uint8_t) i);

        button_attach(&g_buttons[i], BTN_PRESS_DOWN, on_press_down);
        button_attach(&g_buttons[i], BTN_PRESS_UP, on_press_up);
        button_attach(&g_buttons[i], BTN_PRESS_REPEAT, on_press_repeat);
        button_attach(&g_buttons[i], BTN_SINGLE_CLICK, on_single_click);
        button_attach(&g_buttons[i], BTN_DOUBLE_CLICK, on_double_click);
        button_attach(&g_buttons[i], BTN_LONG_PRESS_START, on_long_press_start);
        button_attach(&g_buttons[i], BTN_LONG_PRESS_HOLD, on_long_press_hold);

        if (button_start(&g_buttons[i]) != 0) {
            return false;
        }
    }

    g_next_tick = make_timeout_time_ms(UI_INPUT_TICK_MS);
    g_ready = true;
    return true;
}

void ui_input_backend_task(void) {
    if (!g_ready) {
        return;
    }

    absolute_time_t now = get_absolute_time();

    uint32_t steps = 0;
    while (steps < UI_INPUT_MAX_TICKS_PER_TASK && absolute_time_diff_us(now, g_next_tick) <= 0) {
        button_ticks();
        g_counters.tick_calls++;
        g_next_tick = delayed_by_ms(g_next_tick, UI_INPUT_TICK_MS);
        steps++;
    }
}

bool ui_input_backend_pop(ui_input_raw_event_t *out_event) {
    if (!out_event || g_queue_count == 0) {
        return false;
    }

    *out_event = g_events[g_queue_head];
    g_queue_head = (g_queue_head + 1u) % UI_INPUT_EVENT_QUEUE_SIZE;
    g_queue_count--;
    g_counters.events_popped++;
    return true;
}

void ui_input_backend_get_counters(ui_input_backend_counters_t *out_counters) {
    if (!out_counters) {
        return;
    }
    *out_counters = g_counters;
}

#else

bool ui_input_backend_init(void) {
    return true;
}

void ui_input_backend_task(void) {
    (void) 0;
}

bool ui_input_backend_pop(ui_input_raw_event_t *out_event) {
    (void) out_event;
    return false;
}

void ui_input_backend_get_counters(ui_input_backend_counters_t *out_counters) {
    if (!out_counters) {
        return;
    }
    memset(out_counters, 0, sizeof(*out_counters));
}

#endif /* UI_INPUT_ENABLED */
