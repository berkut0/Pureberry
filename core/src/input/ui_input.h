#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tick_calls;
    uint32_t events_generated;
    uint32_t events_dropped;
    uint32_t events_popped;
    uint32_t events_policy_dropped;
    uint32_t events_policy_release_suppressed_after_long;
    uint32_t events_policy_double_ignored;
    uint32_t actions_emitted;
    uint32_t actions_ignored;
    uint32_t actions_process_budget_hit;
} ui_input_stats_t;

bool ui_input_init(void);
void ui_input_task(void);
void ui_input_get_stats(ui_input_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* UI_INPUT_H */
