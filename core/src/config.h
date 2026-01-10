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
// If not defined by CMake, WS2812 is disabled

#endif // CONFIG_H
