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
#include "multicore_audio.h"
#include <stdio.h>
#include <string.h>

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
            if (data2 > 0) {
                midi_stats.note_on_count++;
            } else {
                midi_stats.note_off_count++;
            }
            ctrl_push_notein(data1, data2, channel);
            break;

        case MIDI_NOTE_OFF:
            midi_stats.note_off_count++;
            ctrl_push_notein(data1, 0, channel);
            break;

        case MIDI_CONTROL_CHANGE:
            midi_stats.cc_count++;
            ctrl_push_ctlin(data1, data2, channel);
            break;

        case MIDI_PROGRAM_CHANGE:
            ctrl_push_pgmin(data1, channel);
            break;

        case MIDI_PITCH_BEND: {
            midi_stats.pitch_bend_count++;
            int16_t bend_value = (int16_t)((data2 << 7) | data1);
            ctrl_push_bendin(bend_value, channel);
            break;
        }

        case MIDI_CHANNEL_AFTERTOUCH:
            ctrl_push_touchin(data1, channel);
            break;

        case MIDI_POLY_AFTERTOUCH:
            ctrl_push_polytouchin(data1, data2, channel);
            break;

        default:
            if (status >= MIDI_SYSTEM) printf("[MIDI] System message: 0x%02X\n", status);
            break;
    }
}

void usb_midi_init(void) {
    memset(&midi_stats, 0, sizeof(midi_stats));
    printf("USB MIDI initialized (multicore: ctrl_queue)\n");
}

void usb_midi_task(void) {
    while (tud_midi_available()) {
        uint8_t packet[4];
        if (tud_midi_packet_read(packet)) {
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
