#include "input/ui_input.h"

#include <string.h>

#include "config.h"
#include "input/ui_action_map.h"
#include "input/ui_input_backend.h"
#include "ui/ui_manager.h"

#ifndef UI_INPUT_MAX_EVENTS_PER_TASK
#define UI_INPUT_MAX_EVENTS_PER_TASK 8u
#endif

#ifndef UI_INPUT_HOLD_EMIT_EVERY
#define UI_INPUT_HOLD_EMIT_EVERY 8u
#endif

typedef struct {
    uint32_t events_policy_dropped;
    uint32_t events_policy_release_suppressed_after_long;
    uint32_t events_policy_double_ignored;
    uint32_t actions_emitted;
    uint32_t actions_ignored;
    uint32_t actions_process_budget_hit;
} ui_input_runtime_stats_t;

static bool g_ui_input_initialized;
static ui_input_runtime_stats_t g_runtime_stats;
static bool g_long_press_active[UI_INPUT_BTN_COUNT];
static uint8_t g_hold_emit_counter[UI_INPUT_BTN_COUNT];

static bool ui_input_policy_allow_event(const ui_input_raw_event_t *event) {
    if (!event || event->button_id >= UI_INPUT_BTN_COUNT) {
        g_runtime_stats.events_policy_dropped++;
        return false;
    }

    uint8_t id = (uint8_t) event->button_id;

    switch (event->type) {
        case UI_INPUT_EVENT_PRESS_DOWN:
            g_long_press_active[id] = false;
            g_hold_emit_counter[id] = 0;
            return true;

        case UI_INPUT_EVENT_PRESS_UP:
            if (g_long_press_active[id]) {
                g_long_press_active[id] = false;
                g_hold_emit_counter[id] = 0;
                g_runtime_stats.events_policy_dropped++;
                g_runtime_stats.events_policy_release_suppressed_after_long++;
                return false;
            }
            return true;

        case UI_INPUT_EVENT_LONG_PRESS_START:
            g_long_press_active[id] = true;
            g_hold_emit_counter[id] = 0;
            return true;

        case UI_INPUT_EVENT_LONG_PRESS_HOLD:
            if (!g_long_press_active[id]) {
                g_runtime_stats.events_policy_dropped++;
                return false;
            }
            g_hold_emit_counter[id]++;
            if (g_hold_emit_counter[id] < UI_INPUT_HOLD_EMIT_EVERY) {
                g_runtime_stats.events_policy_dropped++;
                return false;
            }
            g_hold_emit_counter[id] = 0;
            return true;

        case UI_INPUT_EVENT_SINGLE_CLICK:
            // Latency-first policy: short actions are emitted on PRESS_UP.
            g_runtime_stats.events_policy_dropped++;
            return false;

        case UI_INPUT_EVENT_DOUBLE_CLICK:
            // Reserved for future UI behavior, not used in MVP phase.
            g_runtime_stats.events_policy_dropped++;
            g_runtime_stats.events_policy_double_ignored++;
            return false;

        case UI_INPUT_EVENT_PRESS_REPEAT:
            // Not used in current action model.
            g_runtime_stats.events_policy_dropped++;
            return false;

        default:
            return true;
    }
}

bool ui_input_init(void) {
    memset(&g_runtime_stats, 0, sizeof(g_runtime_stats));
    memset(g_long_press_active, 0, sizeof(g_long_press_active));
    memset(g_hold_emit_counter, 0, sizeof(g_hold_emit_counter));

#if UI_INPUT_ENABLED
    g_ui_input_initialized = ui_input_backend_init();
    return g_ui_input_initialized;
#else
    g_ui_input_initialized = false;
    return true;
#endif
}

void ui_input_task(void) {
#if UI_INPUT_ENABLED
    if (!g_ui_input_initialized) {
        return;
    }

    ui_input_backend_task();

    uint32_t processed = 0;
    ui_input_raw_event_t event;
    while (processed < UI_INPUT_MAX_EVENTS_PER_TASK && ui_input_backend_pop(&event)) {
        if (!ui_input_policy_allow_event(&event)) {
            processed++;
            continue;
        }

        ui_action_t action = UI_ACTION_NONE;
        if (ui_action_map_map_event(&event, &action)) {
            ui_post_action(action);
            g_runtime_stats.actions_emitted++;
        } else {
            g_runtime_stats.actions_ignored++;
        }
        processed++;
    }

    if (processed == UI_INPUT_MAX_EVENTS_PER_TASK) {
        g_runtime_stats.actions_process_budget_hit++;
    }
#else
    (void) g_ui_input_initialized;
#endif
}

void ui_input_get_stats(ui_input_stats_t *out_stats) {
    if (!out_stats) {
        return;
    }

    memset(out_stats, 0, sizeof(*out_stats));

    ui_input_backend_counters_t backend = {0};
    ui_input_backend_get_counters(&backend);

    out_stats->tick_calls = backend.tick_calls;
    out_stats->events_generated = backend.events_generated;
    out_stats->events_dropped = backend.events_dropped;
    out_stats->events_popped = backend.events_popped;
    out_stats->events_policy_dropped = g_runtime_stats.events_policy_dropped;
    out_stats->events_policy_release_suppressed_after_long = g_runtime_stats.events_policy_release_suppressed_after_long;
    out_stats->events_policy_double_ignored = g_runtime_stats.events_policy_double_ignored;
    out_stats->actions_emitted = g_runtime_stats.actions_emitted;
    out_stats->actions_ignored = g_runtime_stats.actions_ignored;
    out_stats->actions_process_budget_hit = g_runtime_stats.actions_process_budget_hit;
}
