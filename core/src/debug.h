#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Log levels
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3,
} log_level_t;

// Global log level (can be overridden via compile definitions)
#ifndef FIRMWARE_LOG_LEVEL
#define FIRMWARE_LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOG_ERROR(fmt, ...) \
    do { \
        if (FIRMWARE_LOG_LEVEL >= LOG_LEVEL_ERROR) { \
            printf("[ERROR] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (FIRMWARE_LOG_LEVEL >= LOG_LEVEL_WARN) { \
            printf("[WARN]  " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (FIRMWARE_LOG_LEVEL >= LOG_LEVEL_INFO) { \
            printf("[INFO]  " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (FIRMWARE_LOG_LEVEL >= LOG_LEVEL_DEBUG) { \
            printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#if FIRMWARE_LOG_LEVEL >= LOG_LEVEL_DEBUG
#define DEBUG_CODE(code) code
#else
#define DEBUG_CODE(code)
#endif

#endif // DEBUG_H
