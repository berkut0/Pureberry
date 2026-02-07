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

static void usb_cdc_write_str(const char *s) {
    if (!s) return;
    (void)tud_cdc_write(s, (uint32_t)strlen(s));
}

static void usb_cdc_handle_line(const char *line) {
    if (!line || !line[0]) return;

    if (strcmp(line, "ping") == 0) {
        usb_cdc_write_str("pong\r\n");
        return;
    }
    if (strcmp(line, "help") == 0) {
        usb_cdc_write_str("Commands: help, ping, ver\r\n");
        return;
    }
    if (strcmp(line, "ver") == 0) {
        usb_cdc_write_str("rp2350-puredata (USB CDC debug)\r\n");
        return;
    }

    usb_cdc_write_str("ERR: unknown command (try 'help')\r\n");
}

void usb_cdc_task(void) {
    static bool was_connected;
    static char line_buf[128];
    static size_t line_len;

    bool now_connected = usb_cdc_connected();
    if (!now_connected) {
        was_connected = false;
        line_len = 0;
        return;
    }

    if (!was_connected) {
        usb_cdc_write_str("rp2350-puredata CDC ready\r\n");
        was_connected = true;
    }

    while (tud_cdc_available()) {
        uint8_t buf[64];
        uint32_t n = tud_cdc_read(buf, sizeof(buf));
        if (!n) break;

        // Echo bytes back (debug aid).
        (void)tud_cdc_write(buf, n);

        for (uint32_t i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\r' || c == '\n') {
                if (line_len) {
                    line_buf[line_len] = '\0';
                    usb_cdc_handle_line(line_buf);
                    line_len = 0;
                }
                continue;
            }

            if (line_len + 1 < sizeof(line_buf)) {
                line_buf[line_len++] = c;
            } else {
                // Line too long: reset to avoid partial command execution.
                line_len = 0;
            }
        }
    }

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
