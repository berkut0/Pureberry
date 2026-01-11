/*
 * USB MIDI Implementation for RP2350 Pure Data Firmware
 * 
 * Receives MIDI messages via USB and forwards them to Pure Data patches
 * Uses Pure Data standard MIDI object format for compatibility:
 * - notein: [note, velocity, channel] lists (velocity=0 for Note Off)
 * - ctlin: [controller, value, channel] lists
 * - bendin: [bend, channel] lists
 * - pgmin: [program, channel] lists
 * 
 * Supports: Note On/Off, Control Change, Pitch Bend, Program Change, Aftertouch
 */

#include "usb_midi.h"
#include "tusb.h"
#include "HvHeavy.h"
#include <stdio.h>
#include <string.h>

// Heavy context for sending messages to Pure Data
static HeavyContextInterface *heavy_context = NULL;

// MIDI statistics
static usb_midi_stats_t midi_stats = {0};

// MIDI message types (status byte high nibble)
#define MIDI_NOTE_OFF         0x80
#define MIDI_NOTE_ON          0x90
#define MIDI_POLY_AFTERTOUCH  0xA0
#define MIDI_CONTROL_CHANGE   0xB0
#define MIDI_PROGRAM_CHANGE   0xC0
#define MIDI_CHANNEL_AFTERTOUCH 0xD0
#define MIDI_PITCH_BEND       0xE0
#define MIDI_SYSTEM           0xF0

/**
 * Parse and process a MIDI message
 * @param packet 4-byte USB MIDI packet
 */
static void process_midi_packet(const uint8_t packet[4]) {
    if (heavy_context == NULL) {
        return;
    }

    // USB MIDI packet format:
    // Byte 0: Cable number (high nibble) + Code Index Number (low nibble)
    // Bytes 1-3: MIDI message bytes

    uint8_t cable = packet[0] >> 4;
    uint8_t cin = packet[0] & 0x0F;
    uint8_t status = packet[1];
    uint8_t data1 = packet[2];
    uint8_t data2 = packet[3];

    (void)cable; // Unused for now
    (void)cin;   // CIN tells us message size, but we can infer from status

    // Extract message type and channel
    uint8_t msg_type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    midi_stats.total_messages++;

    switch (msg_type) {
        case MIDI_NOTE_ON:
            // Pure Data notein format: [note, velocity, channel]
            // Note: velocity=0 is treated as Note Off in Pure Data
            if (data2 > 0) {
                midi_stats.note_on_count++;
            } else {
                midi_stats.note_off_count++;
            }
            
            // Always send velocity (even 0 for Note Off) for Pure Data compatibility
            // Use hv_sendMessageToReceiverFFF for [note, velocity, channel] list
            if (hv_sendMessageToReceiverFFF(heavy_context, hv_stringToHash("notein"), 0.0,
                    (double)data1, (double)data2, (double)channel)) {
                printf("[MIDI] notein: note=%d vel=%d ch=%d\n", data1, data2, channel);
            } else {
                printf("[MIDI] notein: note=%d vel=%d ch=%d (queue full)\n", data1, data2, channel);
            }
            break;

        case MIDI_NOTE_OFF:
            midi_stats.note_off_count++;
            
            // Pure Data notein format: [note, velocity=0, channel]
            // Send as [note, 0, channel] to match Pure Data notein behavior
            if (hv_sendMessageToReceiverFFF(heavy_context, hv_stringToHash("notein"), 0.0,
                    (double)data1, 0.0, (double)channel)) {
                printf("[MIDI] notein: note=%d vel=0 ch=%d (Note Off)\n", data1, channel);
            } else {
                printf("[MIDI] notein: note=%d vel=0 ch=%d (Note Off, queue full)\n", data1, channel);
            }
            break;

        case MIDI_CONTROL_CHANGE:
            midi_stats.cc_count++;
            
            // Pure Data ctlin format: [controller, value, channel]
            if (hv_sendMessageToReceiverFFF(heavy_context, hv_stringToHash("ctlin"), 0.0,
                    (double)data1, (double)data2, (double)channel)) {
                printf("[MIDI] ctlin: cc=%d val=%d ch=%d\n", data1, data2, channel);
            } else {
                printf("[MIDI] ctlin: cc=%d val=%d ch=%d (queue full)\n", data1, data2, channel);
            }
            break;

        case MIDI_PROGRAM_CHANGE:
            // Pure Data pgmin format: [program, channel]
            if (hv_sendMessageToReceiverFF(heavy_context, hv_stringToHash("pgmin"), 0.0,
                    (double)data1, (double)channel)) {
                printf("[MIDI] pgmin: prog=%d ch=%d\n", data1, channel);
            } else {
                printf("[MIDI] pgmin: prog=%d ch=%d (queue full)\n", data1, channel);
            }
            break;

        case MIDI_PITCH_BEND:
            midi_stats.pitch_bend_count++;
            
            // Pitch bend is 14-bit: combine data1 (LSB) and data2 (MSB)
            int16_t bend_value = (data2 << 7) | data1;
            // Convert to 0-16383 range (Pure Data bendin uses 0-16383, center = 8192)
            // Pure Data bendin format: [bend, channel] where bend is 0-16383
            double bend_pd = (double)bend_value;
            
            // Pure Data bendin format: [bend, channel]
            if (hv_sendMessageToReceiverFF(heavy_context, hv_stringToHash("bendin"), 0.0,
                    bend_pd, (double)channel)) {
                printf("[MIDI] bendin: bend=%.0f ch=%d\n", bend_pd, channel);
            } else {
                printf("[MIDI] bendin: bend=%.0f ch=%d (queue full)\n", bend_pd, channel);
            }
            break;

        case MIDI_CHANNEL_AFTERTOUCH:
            // Pure Data touchin format: [pressure, channel]
            if (hv_sendMessageToReceiverFF(heavy_context, hv_stringToHash("touchin"), 0.0,
                    (double)data1, (double)channel)) {
                printf("[MIDI] touchin: pressure=%d ch=%d\n", data1, channel);
            } else {
                printf("[MIDI] touchin: pressure=%d ch=%d (queue full)\n", data1, channel);
            }
            break;

        case MIDI_POLY_AFTERTOUCH:
            // Pure Data polytouchin format: [note, pressure, channel]
            if (hv_sendMessageToReceiverFFF(heavy_context, hv_stringToHash("polytouchin"), 0.0,
                    (double)data1, (double)data2, (double)channel)) {
                printf("[MIDI] polytouchin: note=%d pressure=%d ch=%d\n", data1, data2, channel);
            } else {
                printf("[MIDI] polytouchin: note=%d pressure=%d ch=%d (queue full)\n", data1, data2, channel);
            }
            break;

        default:
            // System messages (0xF0-0xFF) or unknown
            if (status >= MIDI_SYSTEM) {
                printf("[MIDI] System message: 0x%02X\n", status);
            }
            break;
    }
}

// Initialize USB MIDI
void usb_midi_init(HeavyContextInterface *context) {
    heavy_context = context;
    memset(&midi_stats, 0, sizeof(midi_stats));
    printf("USB MIDI initialized\n");
}

// Process incoming MIDI messages
void usb_midi_task(void) {
    // Read all available MIDI packets
    while (tud_midi_available()) {
        uint8_t packet[4];
        if (tud_midi_packet_read(packet)) {
            if (heavy_context == NULL) {
                printf("[MIDI] WARNING: heavy_context is NULL, cannot send to Pure Data!\n");
            }
            process_midi_packet(packet);
        }
    }
}

// Check if MIDI is mounted
bool usb_midi_mounted(void) {
    return tud_midi_mounted();
}

// Get MIDI statistics
void usb_midi_get_stats(usb_midi_stats_t *stats) {
    if (stats) {
        memcpy(stats, &midi_stats, sizeof(usb_midi_stats_t));
    }
}

// Reset MIDI statistics
void usb_midi_reset_stats(void) {
    memset(&midi_stats, 0, sizeof(midi_stats));
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when MIDI interface is mounted
void tud_midi_mount_cb(uint8_t itf) {
    (void)itf;
    printf("USB MIDI mounted\n");
}

// Invoked when MIDI interface is unmounted
void tud_midi_umount_cb(uint8_t itf) {
    (void)itf;
    printf("USB MIDI unmounted\n");
}

// Invoked when MIDI RX has data
void tud_midi_rx_cb(uint8_t itf) {
    (void)itf;
    // Data will be processed in usb_midi_task()
}
