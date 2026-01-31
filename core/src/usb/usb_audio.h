/*
 * USB Audio (UAC2 speaker) receive path: host -> device.
 *
 * This module:
 * - Pulls PCM S16LE stereo frames from TinyUSB's audio RX FIFO on core0
 * - Buffers them in a lock-free ring for consumption on core1
 */

#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize internal state (ring buffer, counters). */
void usb_audio_init(void);

/** Service USB audio receive: pull from TinyUSB FIFO into the ring. Call on core0. */
void usb_audio_task(void);

/** Pop up to `frames` stereo frames into `dst_interleaved_i16` (LRLR...). Call on core1. */
size_t usb_audio_pop_i16(int16_t *dst_interleaved_i16, size_t frames);

/** True while the host selected a non-zero alt setting for the audio streaming interface. */
bool usb_audio_is_streaming(void);

/** Current USB audio sample rate as set by the host (e.g., 44100 or 48000). */
uint32_t usb_audio_get_sample_rate(void);

/** Approximate TinyUSB RX FIFO availability in bytes (as last observed by usb_audio_task on core0). */
uint32_t usb_audio_get_last_avail_bytes(void);

/** Number of bytes read from TinyUSB FIFO during the last usb_audio_task call (core0). */
uint32_t usb_audio_get_last_rx_bytes(void);

/** Current fill level of the inter-core ring buffer in stereo frames. */
uint32_t usb_audio_get_ring_fill_frames(void);

/** Last feedback value (16.16 fixed-point frames per USB frame) set by the application. */
uint32_t usb_audio_get_last_feedback_q16_16(void);

/** Count of feedback updates performed by the application. */
uint32_t usb_audio_get_feedback_update_count(void);

/** Cumulative count of frames dropped due to ring-buffer overrun. */
uint32_t usb_audio_get_overrun_frames(void);

/** Count of ring-buffer underrun events (core1 requested data but none/too little was available). */
uint32_t usb_audio_get_underrun_count(void);

/** Count of SET_INTERFACE requests seen for audio streaming (interface 1). */
uint32_t usb_audio_get_set_itf_count(void);

/** Last alternate setting received for audio streaming interface (1). */
uint8_t usb_audio_get_last_alt_setting(void);

/** Last audio class control request recipient (interface=1, endpoint=2). */
uint8_t usb_audio_get_last_req_recipient(void);

/** Last audio class control request bRequest (e.g., CUR=0x01, RANGE=0x02). */
uint8_t usb_audio_get_last_req_bRequest(void);

/** Last audio class control request control selector. */
uint8_t usb_audio_get_last_req_control_selector(void);

/** Last audio class control request entity ID (0 for interface/endpoint requests). */
uint8_t usb_audio_get_last_req_entity_id(void);

/** Last audio class control request wLength. */
uint16_t usb_audio_get_last_req_wLength(void);

/** Count of audio class control requests observed (interface/endpoint/entity). */
uint32_t usb_audio_get_last_req_count(void);

#ifdef __cplusplus
}
#endif

#endif // USB_AUDIO_H
