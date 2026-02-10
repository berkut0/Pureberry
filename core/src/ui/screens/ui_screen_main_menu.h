#ifndef UI_SCREEN_MAIN_MENU_H
#define UI_SCREEN_MAIN_MENU_H

#include "ui/ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Returns main menu screen descriptor, or NULL when unavailable in current build. */
const ui_screen_t *ui_screen_main_menu_get(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_MAIN_MENU_H */
