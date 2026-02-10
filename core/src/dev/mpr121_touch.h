/**
 * MPR121 capacitive touch driver (core0).
 * Reads touch status via IRQ (GPIO12), pushes touch data via patch_api contract.
 */

#ifndef MPR121_TOUCH_H
#define MPR121_TOUCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize MPR121 and GPIO IRQ (core0). */
bool mpr121_touch_init(void);

/** Poll touch state and push changes to patch_api (touch1..touch12 + touchN_level). */
void mpr121_touch_task(void);

#ifdef __cplusplus
}
#endif

#endif /* MPR121_TOUCH_H */
