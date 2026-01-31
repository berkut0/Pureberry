/*
 * USB Descriptors for RP2350 Pure Data Firmware
 * 
 * Composite Device (custom TinyUSB):
 * - CDC (debug serial)
 * - Optional: MIDI (music input)
 * - Optional: Audio (UAC2 speaker with feedback, host -> device)
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
                           _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4) | _PID_MAP(AUDIO, 5) )

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

// Interface numbers depend on enabled classes.
#define ITF_NUM_AUDIO_CONTROL     0
#define ITF_NUM_AUDIO_STREAMING   1

#define ITF_NUM_CDC_CONTROL       (CFG_TUD_AUDIO ? 2 : 0)
#define ITF_NUM_CDC_DATA          (ITF_NUM_CDC_CONTROL + 1)

#define ITF_NUM_MIDI              (ITF_NUM_CDC_CONTROL + 2)
#define ITF_NUM_MIDI_STREAMING    (ITF_NUM_MIDI + 1)

#define ITF_NUM_TOTAL             (ITF_NUM_CDC_CONTROL + 2 + (CFG_TUD_MIDI ? 2 : 0))

// Endpoint addresses
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define EPNUM_MIDI_OUT    0x03
#define EPNUM_MIDI_IN     0x83

#define EPNUM_AUDIO_OUT   0x04
#define EPNUM_AUDIO_FB    0x84

// Audio UAC2 Speaker (stereo) with explicit feedback
#define USB_AUDIO_ENTITY_CLOCK               0x04
#define USB_AUDIO_ENTITY_INPUT_TERMINAL      0x01
#define USB_AUDIO_ENTITY_FEATURE_UNIT        0x02
#define USB_AUDIO_ENTITY_OUTPUT_TERMINAL     0x03

#define USB_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epout, _epoutsize, _epfb, _epfbsize) \
  /* Standard Interface Association Descriptor (IAD) */\
  TUD_AUDIO_DESC_IAD(/*_firstitf*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00),\
  /* Standard AC Interface Descriptor(4.7.1) */\
  TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx),\
  /* Class-Specific AC Interface Header Descriptor(4.7.2) */\
  TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER, /*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+TUD_AUDIO_DESC_OUTPUT_TERM_LEN+TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN, /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),\
  /* Clock Source Descriptor(4.7.2.1) */\
  TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ USB_AUDIO_ENTITY_CLOCK, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_PRO_CLK, /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ USB_AUDIO_ENTITY_INPUT_TERMINAL,  /*_stridx*/ 0x00),\
  /* Input Terminal Descriptor(4.7.2.4) */\
  TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ USB_AUDIO_ENTITY_INPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, /*_clkid*/ USB_AUDIO_ENTITY_CLOCK, /*_nchannelslogical*/ 0x02, /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*_idxchannelnames*/ 0x00, /*_ctrl*/ 0x0000, /*_stridx*/ 0x00),\
  /* Output Terminal Descriptor(4.7.2.5) */\
  TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ USB_AUDIO_ENTITY_OUTPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, /*_assocTerm*/ USB_AUDIO_ENTITY_INPUT_TERMINAL, /*_srcid*/ USB_AUDIO_ENTITY_FEATURE_UNIT, /*_clkid*/ USB_AUDIO_ENTITY_CLOCK, /*_ctrl*/ 0x0000, /*_stridx*/ 0x00),\
  /* Feature Unit Descriptor(4.7.2.8) */\
  TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL(/*_unitid*/ USB_AUDIO_ENTITY_FEATURE_UNIT, /*_srcid*/ USB_AUDIO_ENTITY_INPUT_TERMINAL, /*_ctrlch0master*/ (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS) | (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS), /*_ctrlch1*/ (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS) | (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS), /*_ctrlch2*/ (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS) | (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS), /*_stridx*/ 0x00),\
  /* Standard AS Interface Descriptor(4.9.1) */\
  /* Streaming Interface, Alternate 0 - 0 bandwidth */\
  TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, /*_stridx*/ 0x00),\
  /* Standard AS Interface Descriptor(4.9.1) */\
  /* Streaming Interface, Alternate 1 - data streaming */\
  TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x01, /*_nEPs*/ 0x02, /*_stridx*/ 0x00),\
  /* Class-Specific AS Interface Descriptor(4.9.2) */\
  TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ USB_AUDIO_ENTITY_INPUT_TERMINAL, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x02, /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*_stridx*/ 0x00),\
  /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */\
  TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample),\
  /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */\
  TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, /*_attr*/ (uint8_t) ((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epoutsize, /*_interval*/ 0x01),\
  /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */\
  TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, /*_ctrl*/ AUDIO_CTRL_NONE, /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, /*_lockdelay*/ 0x0001),\
  /* Standard AS Isochronous Feedback Endpoint Descriptor(4.10.2.1) */\
  TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(/*_ep*/ _epfb, /*_epsize*/ _epfbsize, /*_interval*/ (TUD_OPT_HIGH_SPEED ? 4 : 1))

#if CFG_TUD_AUDIO
#define USB_AUDIO_DESC_LEN CFG_TUD_AUDIO_FUNC_1_DESC_LEN
#else
#define USB_AUDIO_DESC_LEN 0
#endif

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN \
                          + USB_AUDIO_DESC_LEN \
                          + TUD_CDC_DESC_LEN \
                          + (CFG_TUD_MIDI ? TUD_MIDI_DESC_LEN : 0))

// Full speed configuration
uint8_t const desc_fs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Audio (UAC2 speaker with feedback): Interface number, string index, bytes/sample, bits/sample, EP OUT, EP size, EP FB, EP FB size
#if CFG_TUD_AUDIO
    USB_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
                                          EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
                                          EPNUM_AUDIO_FB, 4),
#endif

    // CDC: Interface number, string index, EP notification address and size, EP data address (out, in) and size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CONTROL, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // MIDI: Interface number, string index, EP Out & EP In address, EP size
#if CFG_TUD_MIDI
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 6, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
#endif
};

#if TUD_OPT_HIGH_SPEED
// High speed configuration
uint8_t const desc_hs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Audio (UAC2 speaker with feedback)
#if CFG_TUD_AUDIO
    USB_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
                                          EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
                                          EPNUM_AUDIO_FB, 4),
#endif

    // CDC: Interface number, string index, EP notification address and size, EP data address (out, in) and size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CONTROL, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),

    // MIDI: Interface number, string index, EP Out & EP In address, EP size
#if CFG_TUD_MIDI
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 6, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 512)
#endif
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
    STRID_AUDIO_INTERFACE,
    STRID_CDC_INTERFACE,
    STRID_MIDI_INTERFACE,
};

// Array of pointer to string descriptors
char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Language ID (English)
    "RP2350 Pure Data",            // 1: Manufacturer
    "Pure Data Audio Device",      // 2: Product
    NULL,                          // 3: Serial (will use unique ID)
    "Pure Data Audio",             // 4: Audio interface
    "Pure Data Debug",             // 5: CDC Interface
    USB_MIDI_DEVICE_NAME,          // 6: MIDI Interface (configurable via USB_MIDI_DEVICE_NAME)
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
