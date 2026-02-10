/*
 * USB CDC Interface for RP2350 Pure Data Firmware
 * 
 * Provides printf/debug output via USB CDC (serial)
 * Replacement for pico_stdio_usb with manual TinyUSB control
 */

#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize USB CDC for stdio
 * Should be called after tusb_init()
 */
void usb_cdc_init(void);

/**
 * Check if USB CDC is connected (host terminal open)
 */
bool usb_cdc_connected(void);

/**
 * Write characters to USB CDC
 * Used by stdio driver
 */
void usb_cdc_write_chars(const char *buf, int length);

/**
 * Flush USB CDC output buffer
 */
void usb_cdc_flush(void);

/**
 * Read characters from USB CDC (if available)
 * Returns number of characters read
 */
int usb_cdc_read_chars(char *buf, int length);

/**
 * Service CDC I/O.
 *
 * Call from the core0 main loop near `tud_task()`.
 */
void usb_cdc_task(void);

#ifdef __cplusplus
}
#endif

#endif // USB_CDC_H
