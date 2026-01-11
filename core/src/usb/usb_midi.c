/*
 * USB MIDI Implementation for RP2350 Pure Data Firmware
 * 
 * Receives MIDI messages via USB and forwards them to Pure Data patches
 * Supports: Note On/Off, Control Change, Pitch Bend, Program Change
 */

#include "usb_midi.h"
#include "tusb.h"
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
            if (data2 > 0) { // Velocity > 0 = Note On
                midi_stats.note_on_count++;
                
                // Send to Pure Data: [r midi_note_on] receives (note, velocity, channel)
                if (heavy_context != NULL) {
                    hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_note_on"), (float)data1);
                    hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_velocity"), (float)data2);
                    hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
                    printf("[MIDI] Note On: note=%d vel=%d ch=%d (sent to Heavy)\n", data1, data2, channel);
                } else {
                    printf("[MIDI] Note On: note=%d vel=%d ch=%d (WARNING: heavy_context is NULL!)\n", data1, data2, channel);
                }
            } else { // Velocity = 0 is Note Off
                midi_stats.note_off_count++;
                
                // Send to Pure Data: [r midi_note_off] receives (note, channel)
                hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_note_off"), (float)data1);
                hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
                
                printf("[MIDI] Note Off: note=%d ch=%d\n", data1, channel);
            }
            break;

        case MIDI_NOTE_OFF:
            midi_stats.note_off_count++;
            
            // Send to Pure Data: [r midi_note_off] receives (note, channel)
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_note_off"), (float)data1);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
            
            printf("[MIDI] Note Off: note=%d ch=%d\n", data1, channel);
            break;

        case MIDI_CONTROL_CHANGE:
            midi_stats.cc_count++;
            
            // Send to Pure Data: [r midi_cc] receives (cc_number, value, channel)
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_cc_num"), (float)data1);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_cc_val"), (float)data2);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
            
            printf("[MIDI] CC: cc=%d val=%d ch=%d\n", data1, data2, channel);
            break;

        case MIDI_PROGRAM_CHANGE:
            // Send to Pure Data: [r midi_program] receives (program, channel)
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_program"), (float)data1);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
            
            printf("[MIDI] Program Change: prog=%d ch=%d\n", data1, channel);
            break;

        case MIDI_PITCH_BEND:
            midi_stats.pitch_bend_count++;
            
            // Pitch bend is 14-bit: combine data1 (LSB) and data2 (MSB)
            int16_t bend_value = (data2 << 7) | data1;
            // Convert to -1.0 to +1.0 range (center = 8192)
            float bend_normalized = ((float)bend_value - 8192.0f) / 8192.0f;
            
            // Send to Pure Data: [r midi_pitchbend] receives (value, channel)
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_pitchbend"), bend_normalized);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
            
            printf("[MIDI] Pitch Bend: val=%.3f ch=%d\n", bend_normalized, channel);
            break;

        case MIDI_CHANNEL_AFTERTOUCH:
            // Send to Pure Data: [r midi_aftertouch] receives (pressure, channel)
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_aftertouch"), (float)data1);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
            
            printf("[MIDI] Aftertouch: val=%d ch=%d\n", data1, channel);
            break;

        case MIDI_POLY_AFTERTOUCH:
            // Send to Pure Data: [r midi_poly_aftertouch] receives (note, pressure, channel)
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_poly_note"), (float)data1);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_poly_pressure"), (float)data2);
            hv_sendFloatToReceiver(heavy_context, hv_stringToHash("midi_channel"), (float)channel);
            
            printf("[MIDI] Poly Aftertouch: note=%d pressure=%d ch=%d\n", data1, data2, channel);
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
