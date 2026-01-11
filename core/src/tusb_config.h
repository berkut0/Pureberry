/*
 * TinyUSB Configuration for RP2350 Pure Data Firmware
 * 
 * Composite USB Device: CDC (serial debug) + MIDI (music input)
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

// MIDI for music input
#define CFG_TUD_MIDI              1

// Disabled classes
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_VENDOR            0

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
