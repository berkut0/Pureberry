#ifndef UI_SCREEN_WAVEFORM_H
#define UI_SCREEN_WAVEFORM_H

#include "ui/ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Returns waveform screen descriptor, or NULL when unavailable in current build. */
const ui_screen_t *ui_screen_waveform_get(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_WAVEFORM_H */
