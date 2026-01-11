/**
 * Firmware Configuration
 * 
 * Central configuration header for firmware.
 * Options are controlled via CMake target_compile_definitions.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Heavy configuration
// We always compile patches with the name "patch" for simplicity
// This means generated files are always: Heavy_patch.h, hv_patch_new(), etc.

// Optional features
// ENABLE_WS2812 is controlled via CMake option (default: ON)
// ENABLE_USB_MIDI is controlled via CMake option (default: OFF)
// If not defined by CMake, features are disabled

// USB MIDI Configuration
#ifdef ENABLE_USB_MIDI
// USB MIDI is enabled - uses custom TinyUSB with CDC+MIDI
// MIDI receiver names in Pure Data patches:
// - [r midi_note_on]     - receives note number when note on
// - [r midi_note_off]    - receives note number when note off
// - [r midi_velocity]    - receives velocity for note on
// - [r midi_cc_num]      - receives CC number
// - [r midi_cc_val]      - receives CC value
// - [r midi_program]     - receives program change number
// - [r midi_pitchbend]   - receives pitch bend (-1.0 to +1.0)
// - [r midi_aftertouch]  - receives channel aftertouch
// - [r midi_poly_note]   - receives note for poly aftertouch
// - [r midi_poly_pressure] - receives pressure for poly aftertouch
// - [r midi_channel]     - receives MIDI channel (0-15)
#endif

#endif // CONFIG_H
