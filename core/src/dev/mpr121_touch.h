/**
 * MPR121 capacitive touch driver (core0).
 * Reads touch status via IRQ (GPIO12), pushes touch1..touch12 (@hv_param) to ctrl_queue.
 */

#ifndef MPR121_TOUCH_H
#define MPR121_TOUCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize MPR121 and GPIO IRQ (core0). */
bool mpr121_touch_init(void);

/** Poll touch state and push changes to touch1..touch12. Call from core0 main loop. */
void mpr121_touch_task(void);

#ifdef __cplusplus
}
#endif

#endif /* MPR121_TOUCH_H */
