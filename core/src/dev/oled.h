#ifndef OLED_H
#define OLED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize SSD1306 OLED over I2C (core0 only). */
bool oled_init(void);

/** Periodic OLED update (core0 only). */
void oled_task(void);

#ifdef __cplusplus
}
#endif

#endif // OLED_H
