/*
 * USB CDC Implementation for RP2350 Pure Data Firmware
 * 
 * Provides printf/debug output via USB CDC (serial)
 * Based on pico_stdio_usb implementation but with manual TinyUSB control
 */

#include "usb_cdc.h"
#include "tusb.h"
#include "pico/stdio/driver.h"
#include "pico/binary_info.h"
#include <stdio.h>
#include <string.h>

// Initialize USB CDC
void usb_cdc_init(void) {
    // Register binary info
    bi_decl(bi_program_feature("USB CDC Debug"));
}

// Check if USB CDC is connected
bool usb_cdc_connected(void) {
    return tud_inited() && tud_cdc_connected();
}

// Write characters to USB CDC
void usb_cdc_write_chars(const char *buf, int length) {
    if (!buf || length <= 0) return;
    if (!usb_cdc_connected()) return;

    // Best-effort write: do not call tud_task() here (serviced from main loop).
    int written_total = 0;
    while (written_total < length) {
        uint32_t avail = tud_cdc_write_available();
        if (!avail) break;

        uint32_t chunk = (uint32_t)(length - written_total);
        if (chunk > avail) chunk = avail;

        uint32_t n = tud_cdc_write(buf + written_total, chunk);
        if (!n) break;
        written_total += (int)n;
    }
}

// Flush USB CDC output
void usb_cdc_flush(void) {
    if (!usb_cdc_connected()) return;
    (void)tud_cdc_write_flush();
}

// Read characters from USB CDC
int usb_cdc_read_chars(char *buf, int length) {
    if (!usb_cdc_connected() || !tud_cdc_available()) {
        return 0;
    }
    
    uint32_t count = tud_cdc_read(buf, (uint32_t)length);
    return (int)count;
}

void usb_cdc_task(void) {
    if (!usb_cdc_connected()) return;
    (void)tud_cdc_write_flush();
}

//--------------------------------------------------------------------+
// STDIO Driver Implementation
//--------------------------------------------------------------------+

static void stdio_usb_out_chars(const char *buf, int length) {
    usb_cdc_write_chars(buf, length);
}

static void stdio_usb_out_flush(void) {
    usb_cdc_flush();
}

static int stdio_usb_in_chars(char *buf, int length) {
    return usb_cdc_read_chars(buf, length);
}

// STDIO driver for USB CDC
stdio_driver_t stdio_usb = {
    .out_chars = stdio_usb_out_chars,
    .out_flush = stdio_usb_out_flush,
    .in_chars = stdio_usb_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF
#endif
};

// Auto-register stdio driver
static void __attribute__((constructor)) stdio_usb_init_driver(void) {
    stdio_set_driver_enabled(&stdio_usb, true);
}
