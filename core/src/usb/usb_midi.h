/*
 * USB MIDI Interface for RP2350 Pure Data Firmware
 * 
 * Receives MIDI messages via USB and forwards them to Pure Data patches
 * through Heavy context receive objects
 */

#ifndef USB_MIDI_H
#define USB_MIDI_H

#include <stdint.h>
#include <stdbool.h>
#include "HvHeavy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize USB MIDI with Heavy context
 * @param context Heavy context to send MIDI data to
 */
void usb_midi_init(HeavyContextInterface *context);

/**
 * Process incoming MIDI messages from USB
 * Should be called regularly in main loop
 */
void usb_midi_task(void);

/**
 * Check if MIDI is mounted and ready
 */
bool usb_midi_mounted(void);

/**
 * Get MIDI statistics (for debugging)
 */
typedef struct {
    uint32_t note_on_count;
    uint32_t note_off_count;
    uint32_t cc_count;
    uint32_t pitch_bend_count;
    uint32_t total_messages;
} usb_midi_stats_t;

void usb_midi_get_stats(usb_midi_stats_t *stats);
void usb_midi_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // USB_MIDI_H
