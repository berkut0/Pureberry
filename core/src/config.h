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
// Uses Pure Data standard MIDI object format for compatibility
// MIDI receiver names in Pure Data patches (standard Pure Data format):
// - [r notein]          - receives [note, velocity, channel] list
//                        Note On: velocity > 0, Note Off: velocity = 0
// - [r ctlin]           - receives [controller, value, channel] list
// - [r bendin]          - receives [bend, channel] list (bend: 0-16383, center=8192)
// - [r pgmin]           - receives [program, channel] list
// - [r touchin]         - receives [pressure, channel] list (channel aftertouch)
// - [r polytouchin]     - receives [note, pressure, channel] list (poly aftertouch)
//
// These match Pure Data's standard MIDI objects for easy patch porting
#endif

#endif // CONFIG_H
