/*
 * USB Descriptors for RP2350 Pure Data Firmware
 * 
 * Composite Device: CDC (debug serial) + MIDI (music input)
 * Based on TinyUSB examples with IAD for Windows compatibility
 */

#include "tusb.h"
#include "pico/unique_id.h"
#include "config.h"

// Fallback for USB_MIDI_DEVICE_NAME if not defined in config.h
#ifndef USB_MIDI_DEVICE_NAME
#define USB_MIDI_DEVICE_NAME "Pure Data MIDI"
#endif

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

// Auto-generate PID based on enabled interfaces
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
                           _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4) )

#define USB_VID   0xCafe
#define USB_BCD   0x0200

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    // Use Interface Association Descriptor (IAD) for composite CDC+MIDI
    // Required by USB Specs: IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum {
    ITF_NUM_CDC_CONTROL = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

// Endpoint addresses
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define EPNUM_MIDI_OUT    0x03
#define EPNUM_MIDI_IN     0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

// Full speed configuration
uint8_t const desc_fs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // CDC: Interface number, string index, EP notification address and size, EP data address (out, in) and size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CONTROL, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // MIDI: Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
};

#if TUD_OPT_HIGH_SPEED
// High speed configuration
uint8_t const desc_hs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // CDC: Interface number, string index, EP notification address and size, EP data address (out, in) and size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CONTROL, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),

    // MIDI: Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 512)
};
#endif

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index; // for multiple configurations

#if TUD_OPT_HIGH_SPEED
    // Although we are highspeed, host may be fullspeed
    return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
    return desc_fs_configuration;
#endif
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// String Descriptor Index
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC_INTERFACE,
    STRID_MIDI_INTERFACE,
};

// Array of pointer to string descriptors
char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Language ID (English)
    "RP2350 Pure Data",            // 1: Manufacturer
    "Pure Data Audio Device",      // 2: Product
    NULL,                          // 3: Serial (will use unique ID)
    "Pure Data Debug",             // 4: CDC Interface
    USB_MIDI_DEVICE_NAME,          // 5: MIDI Interface (configurable via USB_MIDI_DEVICE_NAME)
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case STRID_SERIAL:
            // Use Pico unique ID as serial number
            {
                pico_unique_board_id_t board_id;
                pico_get_unique_board_id(&board_id);
                
                chr_count = 16; // 8 bytes = 16 hex chars
                for (size_t i = 0; i < 8 && i < chr_count / 2; i++) {
                    uint8_t byte = board_id.id[i];
                    _desc_str[1 + i * 2] = "0123456789ABCDEF"[byte >> 4];
                    _desc_str[1 + i * 2 + 1] = "0123456789ABCDEF"[byte & 0x0F];
                }
            }
            break;

        default:
            // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors
            // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

            if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
                return NULL;
            }

            const char *str = string_desc_arr[index];

            // Cap at max char
            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
            if (chr_count > max_count) {
                chr_count = max_count;
            }

            // Convert ASCII string into UTF-16
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }

    // First byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}
