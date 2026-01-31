/*
 * Pico SDK global configuration header.
 *
 * This file is included by pico-sdk's pico.h when PICO_CONFIG_HEADER is defined.
 * It must only contain preprocessor directives (safe for inclusion from assembly).
 */

#ifndef PROJECT_PICO_CONFIG_H
#define PROJECT_PICO_CONFIG_H

// Increase default stack sizes. With OLED (u8g2) + USB composite devices, the default
// 0x800 bytes can be too tight and lead to hard-to-debug lockups.
#ifndef PICO_STACK_SIZE
#define PICO_STACK_SIZE _u(0x1000)
#endif

#ifndef PICO_CORE1_STACK_SIZE
#define PICO_CORE1_STACK_SIZE PICO_STACK_SIZE
#endif

// Replace pico-sdk panic() with a project-defined handler that records crash info and reboots.
// Implemented in src/crash.c
#ifndef PICO_PANIC_FUNCTION
#define PICO_PANIC_FUNCTION project_panic_handler
#endif

// Keep the default pico-sdk config behavior (config_autogen.h etc.)
#include "pico/config.h"

// Project configuration (may include local overrides).
#include "config.h"

#endif // PROJECT_PICO_CONFIG_H

