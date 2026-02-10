#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdbool.h>

#include "ui/ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize UI subsystem on core0.
 * Safe to call in all builds; becomes a no-op when OLED support is disabled.
 */
bool ui_init(void);

/**
 * Run periodic UI processing on core0.
 * Safe to call each main loop iteration.
 */
void ui_task(void);

/**
 * Replace active screen.
 * Returns false when the requested screen is invalid or UI is not initialized.
 */
bool ui_set_screen(const ui_screen_t *screen);

/**
 * Dispatch a logical input action to the active screen.
 */
void ui_post_action(ui_action_t action);

/**
 * Switch to waveform screen.
 */
bool ui_show_waveform(void);

/**
 * Switch to main menu screen.
 */
bool ui_show_main_menu(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MANAGER_H */
