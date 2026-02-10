/*
 * TinyUSB Configuration for RP2350 Pure Data Firmware
 * 
 * Composite USB Device (custom TinyUSB mode):
 * - CDC (serial debug)
 * - Optional: MIDI
 * - Optional: Audio (UAC2 speaker, host -> device)
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for device
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

// Defined by CMake for RP2350
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

// RHPort mode - Device mode
#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#endif

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// Device Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//

// CDC for debug/printf output (replaces pico_stdio_usb)
#define CFG_TUD_CDC               1

#ifdef ENABLE_USB_MIDI
#define CFG_TUD_MIDI              1
#else
#define CFG_TUD_MIDI              0
#endif

#ifdef ENABLE_USB_AUDIO
#define CFG_TUD_AUDIO             1
#else
#define CFG_TUD_AUDIO             0
#endif

// TinyUSB task event queue:
// Default is 16 events. With USB Audio (iso OUT + feedback IN) plus periodic OLED I2C transfers,
// core0 may temporarily block long enough to overflow the queue, triggering an assert.
// Increase the queue depth to tolerate short bursts.
#if CFG_TUD_AUDIO
#ifndef CFG_TUD_TASK_QUEUE_SZ
#define CFG_TUD_TASK_QUEUE_SZ     256
#endif
#endif

// Disabled classes
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_VENDOR            0

//------------- AUDIO (UAC2 speaker + feedback) -------------//

#if CFG_TUD_AUDIO

// Audio function descriptor length (must match descriptors)
#define USB_AUDIO_SPEAKER_STEREO_FB_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN\
  + TUD_AUDIO_DESC_STD_AC_LEN\
  + TUD_AUDIO_DESC_CS_AC_LEN\
  + TUD_AUDIO_DESC_CLK_SRC_LEN\
  + TUD_AUDIO_DESC_INPUT_TERM_LEN\
  + TUD_AUDIO_DESC_OUTPUT_TERM_LEN\
  + TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN\
  + TUD_AUDIO_DESC_STD_AS_INT_LEN\
  + TUD_AUDIO_DESC_STD_AS_INT_LEN\
  + TUD_AUDIO_DESC_CS_AS_INT_LEN\
  + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN\
  + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN\
  + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN\
  + TUD_AUDIO_DESC_STD_AS_ISO_FB_EP_LEN)

#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                                USB_AUDIO_SPEAKER_STEREO_FB_DESC_LEN

// Audio format type I specifications
#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE                         48000

#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX                           2

// 16-bit in 16-bit slots (PCM S16LE)
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX                   2
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX                           16

// Enable OUT endpoint (speaker data)
#define CFG_TUD_AUDIO_ENABLE_EP_OUT                                  1

// Max packet size and software FIFO size
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX                            TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)
// Full-speed isoch OUT can still require a fairly deep FIFO if core0 occasionally stalls (OLED I2C, other work).
// Increase headroom to reduce audible dropouts.
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ                         (TUD_OPT_HIGH_SPEED ? 32 : 32) * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX

// Enable feedback endpoint (required for async sink; Windows expects 4-byte feedback even on FS)
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP                             1

// Number of AS interfaces in this audio function
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 	                             1

// Size of control request buffer
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ	                         64

#endif // CFG_TUD_AUDIO

//------------- CDC -------------//

// CDC FIFO size of TX and RX
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

// CDC Endpoint transfer buffer size, more is faster
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

//------------- MIDI -------------//

// MIDI FIFO size of TX and RX
#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE  (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE  (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

// MIDI Endpoint transfer buffer size
#ifndef CFG_TUD_MIDI_EP_BUFSIZE
#define CFG_TUD_MIDI_EP_BUFSIZE  (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
