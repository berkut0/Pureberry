/*
 * USB CDC Implementation for RP2350 Pure Data Firmware
 * 
 * Provides printf/debug output via USB CDC (serial)
 * Based on pico_stdio_usb implementation but with manual TinyUSB control
 */

#include "usb_cdc.h"
#include "tusb.h"
#include "pico/time.h"
#include "pico/stdio/driver.h"
#include "pico/binary_info.h"
#include <stdio.h>
#include <string.h>

// Timeout for stdout writes (microseconds)
#define USB_CDC_STDOUT_TIMEOUT_US 500000

// Initialize USB CDC
void usb_cdc_init(void) {
    // Register binary info
    bi_decl(bi_program_feature("USB CDC Debug"));
}

// Check if USB CDC is connected
bool usb_cdc_connected(void) {
    return tud_cdc_connected();
}

// Write characters to USB CDC
void usb_cdc_write_chars(const char *buf, int length) {
    static uint64_t last_avail_time = 0;
    
    if (usb_cdc_connected()) {
        for (int i = 0; i < length;) {
            int n = length - i;
            int avail = (int) tud_cdc_write_available();
            
            if (n > avail) {
                n = avail;
            }
            
            if (n) {
                int n2 = (int) tud_cdc_write(buf + i, (uint32_t)n);
                tud_task(); // Service USB
                tud_cdc_write_flush();
                i += n2;
                last_avail_time = time_us_64();
            } else {
                tud_task(); // Service USB
                tud_cdc_write_flush();
                
                // Timeout if buffer full for too long
                if (!usb_cdc_connected() ||
                    (!tud_cdc_write_available() && time_us_64() > last_avail_time + USB_CDC_STDOUT_TIMEOUT_US)) {
                    break;
                }
            }
        }
    } else {
        // Reset timeout if not connected
        last_avail_time = 0;
    }
}

// Flush USB CDC output
void usb_cdc_flush(void) {
    do {
        tud_task(); // Service USB
    } while (tud_cdc_write_flush());
}

// Read characters from USB CDC
int usb_cdc_read_chars(char *buf, int length) {
    if (!usb_cdc_connected() || !tud_cdc_available()) {
        return 0;
    }
    
    uint32_t count = tud_cdc_read(buf, (uint32_t)length);
    return (int)count;
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
