/*
 * Crash reporting helpers.
 *
 * Stores minimal fault info in watchdog scratch registers so it survives a reboot.
 * Intended for on-device debugging (OLED) when USB/stdio is unreliable.
 */

#ifndef CRASH_H
#define CRASH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CRASH_REASON_NONE = 0,
    CRASH_REASON_HARDFAULT = 1,
    CRASH_REASON_PANIC = 2,
} crash_reason_t;

typedef struct {
    crash_reason_t reason;
    uint8_t core_id;
    uint32_t pc;
    uint32_t lr;
    // For CRASH_REASON_PANIC: first variadic argument passed to panic(fmt, ...).
    // For other reasons: 0.
    uint32_t arg0;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
} crash_info_t;

/** Read crash info from watchdog scratch registers. Returns false if none is present. */
bool crash_read(crash_info_t *out);

/** Clear stored crash info. */
void crash_clear(void);

#endif // CRASH_H
