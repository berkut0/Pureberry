#ifndef OLED_H
#define OLED_H

#include <stdbool.h>
#include <stddef.h>

#ifdef ENABLE_OLED
#include "u8g2.h"
typedef u8g2_t oled_canvas_t;
#else
typedef struct u8g2_struct oled_canvas_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize SSD1306 OLED backend over I2C (core0 only). */
bool oled_backend_init(void);

/** Returns backend readiness state. */
bool oled_backend_ready(void);

/** Get active u8g2 canvas; returns NULL when backend is unavailable. */
oled_canvas_t *oled_backend_u8g2(void);

/** Returns true when a new framebuffer flush can be queued immediately. */
bool oled_backend_can_flush(void);

/** Flush current framebuffer to OLED (blocking or DMA-backed, depending on build). */
void oled_backend_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_H */
