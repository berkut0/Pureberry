/*
 * USB Audio (UAC2 speaker) receive implementation.
 *
 * Design notes:
 * - TinyUSB buffers received isochronous OUT data in its internal FIFO (tud_audio_available/read).
 * - core0 periodically pulls data from that FIFO into a shared ring buffer.
 * - core1 consumes from the ring and feeds Heavy input (adc~) as float.
 *
 * Feedback:
 * - We use explicit feedback endpoint, but compute feedback in the application based on the inter-core ring fill.
 */

#include "usb_audio.h"

#include "config.h"
#include "tusb.h"

#include "hardware/structs/usb_dpram.h"

#include <string.h>

#include "pico/time.h"

// This module is only built when ENABLE_USB_AUDIO is ON, but keep a hard guard.
#if !CFG_TUD_AUDIO
#error "usb_audio.c built but CFG_TUD_AUDIO is not enabled"
#endif

#ifndef USB_AUDIO_RING_FRAMES
#define USB_AUDIO_RING_FRAMES 2048u
#endif

#ifndef USB_AUDIO_TARGET_FILL_FRAMES
#define USB_AUDIO_TARGET_FILL_FRAMES 512u
#endif

// Pull in moderate chunks to keep stack usage low.
#ifndef USB_AUDIO_PULL_CHUNK_FRAMES
#define USB_AUDIO_PULL_CHUNK_FRAMES 64u
#endif

#ifndef USB_AUDIO_MAX_PULL_CHUNKS_PER_TASK
#define USB_AUDIO_MAX_PULL_CHUNKS_PER_TASK 8u
#endif

#ifndef USB_AUDIO_FEEDBACK_UPDATE_PERIOD_MS
#define USB_AUDIO_FEEDBACK_UPDATE_PERIOD_MS 1u
#endif

#ifndef USB_AUDIO_FEEDBACK_AVG_SHIFT
#define USB_AUDIO_FEEDBACK_AVG_SHIFT 5u
#endif

#ifndef USB_AUDIO_FEEDBACK_ERR_DEADBAND_FRAMES
#define USB_AUDIO_FEEDBACK_ERR_DEADBAND_FRAMES 2
#endif

#ifndef USB_AUDIO_FEEDBACK_KP_Q16_16
// Q16.16 proportional gain: 8 = 1/8192 frame-per-USB-frame per 1 frame of ring error.
#define USB_AUDIO_FEEDBACK_KP_Q16_16 8
#endif

#ifndef USB_AUDIO_FEEDBACK_KI_Q16_16
// Q16.16 integrator gain: 1 = 1/65536 frame-per-USB-frame per 1 frame of ring error per update.
#define USB_AUDIO_FEEDBACK_KI_Q16_16 1
#endif

#ifndef USB_AUDIO_FEEDBACK_INT_CLAMP_Q16_16
// Integrator clamp: +/-0.25 frame per USB frame.
#define USB_AUDIO_FEEDBACK_INT_CLAMP_Q16_16 (1u << 14)
#endif

#ifndef USB_AUDIO_FEEDBACK_MAX_DELTA_Q16_16
// Max correction from nominal feedback: +/-0.25 frame per USB frame.
#define USB_AUDIO_FEEDBACK_MAX_DELTA_Q16_16 (1u << 14)
#endif

#if (USB_AUDIO_RING_FRAMES < 128)
#error "USB_AUDIO_RING_FRAMES too small"
#endif

#if (USB_AUDIO_TARGET_FILL_FRAMES >= USB_AUDIO_RING_FRAMES)
#error "USB_AUDIO_TARGET_FILL_FRAMES must be < USB_AUDIO_RING_FRAMES"
#endif

// UAC2 entity IDs (must match descriptors)
#define USB_AUDIO_ENTITY_CLOCK           0x04
#define USB_AUDIO_ENTITY_FEATURE_UNIT    0x02

// Audio streaming interface number (must match descriptors: control=0, streaming=1)
#define USB_AUDIO_ITF_STREAMING          0x01

// Endpoints used by this firmware (must match usb_descriptors.c)
#define USB_AUDIO_EP_OUT                 0x04
#define USB_AUDIO_EP_FB                  0x84

static void usb_audio_hw_disable_endpoints(void) {
    // TinyUSB's rp2040 (and rp2350) USB driver can panic with:
    //   "ep %02X was already available"
    // when a new transfer is queued on an endpoint whose DPRAM buffer control AVAIL bit
    // was left set (observed on Windows when toggling audio alt settings).
    //
    // With TUP_DCD_EDPT_ISO_ALLOC ports, TinyUSB's audio class doesn't "close" isochronous
    // endpoints at the DCD level when switching to alt=0. Proactively clearing the DPRAM
    // state makes alt switching robust without patching pico-sdk.
    uint8_t const epnum = (uint8_t)(USB_AUDIO_EP_OUT & 0x0Fu);
    if (epnum == 0) return;

    usb_dpram->ep_buf_ctrl[epnum].out = 0;
    usb_dpram->ep_buf_ctrl[epnum].in = 0;
    usb_dpram->ep_ctrl[epnum - 1].out = 0;
    usb_dpram->ep_ctrl[epnum - 1].in = 0;
}

// Ring buffer (frames are stereo int16 interleaved).
static int16_t usb_audio_ring[USB_AUDIO_RING_FRAMES * 2];
static volatile uint32_t usb_audio_wr_frames;
static volatile uint32_t usb_audio_rd_frames;

// NOTE: Only ring indices are accessed cross-core and need atomics.
// Avoid C11-style atomics on sub-word types (bool/uint8/uint16) since some ARM targets
// may implement them with word-exclusive accesses that require alignment.
static volatile uint32_t usb_audio_streaming;

static volatile uint32_t usb_audio_last_avail_bytes;
static volatile uint32_t usb_audio_last_rx_bytes;
static volatile uint32_t usb_audio_overrun_frames;
static volatile uint32_t usb_audio_underrun_count;
static volatile uint32_t usb_audio_set_itf_count;
static volatile uint8_t usb_audio_last_alt_setting;
static uint32_t usb_audio_ring_fill_min_frames;
static uint32_t usb_audio_ring_fill_max_frames;
static bool usb_audio_ring_fill_window_valid;

// Feedback diagnostics (16.16 fixed-point frames per USB frame)
static volatile uint32_t usb_audio_last_fb_q16_16;
static volatile uint32_t usb_audio_fb_update_count;
static uint32_t usb_audio_fb_fill_avg_q16_16;
static uint32_t usb_audio_fb_last_update_ms;
static bool usb_audio_fb_state_valid;
static int32_t usb_audio_fb_i_q16_16;

// Last observed class-specific control request (helps debug host behavior).
static volatile uint32_t usb_audio_last_req_count;
static volatile uint8_t usb_audio_last_req_recipient;
static volatile uint8_t usb_audio_last_req_bRequest;
static volatile uint8_t usb_audio_last_req_control_selector;
static volatile uint8_t usb_audio_last_req_entity_id;
static volatile uint16_t usb_audio_last_req_wLength;

// Control state (UAC2)
#define USB_AUDIO_SAMPLE_RATE_HZ 48000u
#define USB_AUDIO_BYTES_PER_FRAME \
    ((uint32_t)CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX * (uint32_t)CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)

// AudioControl interface latency (optional control). Keep as a simple 2-byte CUR value.
static volatile int16_t current_latency = 0;

// Feature unit controls: channel 0 = master, 1 = left, 2 = right
static uint8_t mute[3];
static int16_t volume_q8_8[3]; // 1/256 dB units

static inline uint32_t usb_audio_ring_fill_frames(void);
static inline void usb_audio_ring_window_reset(uint32_t fill);
static inline void usb_audio_ring_window_observe(uint32_t fill);

static inline uint32_t usb_audio_nominal_feedback_q16_16(uint32_t sample_rate_hz) {
    // Feedback value is number of audio frames per USB frame in 16.16 format.
    // Full-speed: 1 ms frames; High-speed: 125 us microframes.
    uint32_t const frame_div = (tud_speed_get() == TUSB_SPEED_HIGH) ? 8000u : 1000u;
    if (sample_rate_hz == 0u) sample_rate_hz = USB_AUDIO_SAMPLE_RATE_HZ;
    return (uint32_t)((((uint64_t)sample_rate_hz) << 16) / (uint64_t)frame_div);
}

static void usb_audio_feedback_reset_state(void) {
    usb_audio_fb_fill_avg_q16_16 = 0u;
    usb_audio_fb_last_update_ms = 0u;
    usb_audio_fb_state_valid = false;
    usb_audio_fb_i_q16_16 = 0;
}

static void usb_audio_update_feedback(void) {
    if (!usb_audio_is_streaming()) return;
    if (!tud_audio_mounted()) return;

    // Update less frequently than every loop iteration to reduce control noise.
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (usb_audio_fb_last_update_ms != 0u) {
        uint32_t elapsed_ms = (uint32_t)(now_ms - usb_audio_fb_last_update_ms);
        if (elapsed_ms < (uint32_t)USB_AUDIO_FEEDBACK_UPDATE_PERIOD_MS) {
            return;
        }
    }
    usb_audio_fb_last_update_ms = now_ms;

    uint32_t const sample_rate = usb_audio_get_sample_rate();
    uint32_t const nominal = usb_audio_nominal_feedback_q16_16(sample_rate);

    // Low-pass the ring fill (frames) to avoid jittery feedback.
    uint32_t fill_frames = usb_audio_ring_fill_frames();
    if (!usb_audio_fb_state_valid) {
        usb_audio_fb_fill_avg_q16_16 = (fill_frames << 16);
        usb_audio_fb_state_valid = true;
    } else {
        uint32_t const avg_keep = (1u << USB_AUDIO_FEEDBACK_AVG_SHIFT) - 1u;
        usb_audio_fb_fill_avg_q16_16 =
            (usb_audio_fb_fill_avg_q16_16 * avg_keep + (fill_frames << 16)) >> USB_AUDIO_FEEDBACK_AVG_SHIFT;
    }

    int32_t const fill_avg_frames = (int32_t)(usb_audio_fb_fill_avg_q16_16 >> 16);
    int32_t const target = (int32_t)USB_AUDIO_TARGET_FILL_FRAMES;
    int32_t err = target - fill_avg_frames; // + => buffer low => ask host to send faster
    if (err > -(int32_t)USB_AUDIO_FEEDBACK_ERR_DEADBAND_FRAMES &&
        err < (int32_t)USB_AUDIO_FEEDBACK_ERR_DEADBAND_FRAMES) {
        err = 0;
    }

    int32_t const p_term = err * (int32_t)USB_AUDIO_FEEDBACK_KP_Q16_16;
    usb_audio_fb_i_q16_16 += err * (int32_t)USB_AUDIO_FEEDBACK_KI_Q16_16;
    int32_t const i_clamp = (int32_t)USB_AUDIO_FEEDBACK_INT_CLAMP_Q16_16;
    if (usb_audio_fb_i_q16_16 > i_clamp) usb_audio_fb_i_q16_16 = i_clamp;
    if (usb_audio_fb_i_q16_16 < -i_clamp) usb_audio_fb_i_q16_16 = -i_clamp;

    // PI controller: P reacts quickly, I removes steady-state drift between clocks.
    int32_t fb = (int32_t)nominal + p_term + usb_audio_fb_i_q16_16;

    // Clamp to nominal +/- configured correction window.
    int32_t const max_delta = (int32_t)USB_AUDIO_FEEDBACK_MAX_DELTA_Q16_16;
    int32_t const min_fb = (int32_t)nominal - max_delta;
    int32_t const max_fb = (int32_t)nominal + max_delta;
    if (fb < min_fb) fb = min_fb;
    if (fb > max_fb) fb = max_fb;

    // Update stored value; TinyUSB will transmit the current value on the feedback EP.
    // Even if the EP is busy, this updates the value used for the next packet.
    (void)tud_audio_fb_set((uint32_t)fb);
    usb_audio_last_fb_q16_16 = (uint32_t)fb;
    usb_audio_fb_update_count++;
}

static inline uint32_t usb_audio_ring_fill_frames(void) {
    uint32_t wr = __atomic_load_n(&usb_audio_wr_frames, __ATOMIC_ACQUIRE);
    uint32_t rd = __atomic_load_n(&usb_audio_rd_frames, __ATOMIC_ACQUIRE);
    return wr - rd;
}

static inline uint32_t usb_audio_ring_free_frames(uint32_t fill) {
    return USB_AUDIO_RING_FRAMES - fill;
}

static inline void usb_audio_ring_window_reset(uint32_t fill) {
    usb_audio_ring_fill_min_frames = fill;
    usb_audio_ring_fill_max_frames = fill;
    usb_audio_ring_fill_window_valid = true;
}

static inline void usb_audio_ring_window_observe(uint32_t fill) {
    if (!usb_audio_ring_fill_window_valid) {
        usb_audio_ring_window_reset(fill);
        return;
    }
    if (fill < usb_audio_ring_fill_min_frames) {
        usb_audio_ring_fill_min_frames = fill;
    }
    if (fill > usb_audio_ring_fill_max_frames) {
        usb_audio_ring_fill_max_frames = fill;
    }
}

static void usb_audio_ring_reset(void) {
    __atomic_store_n(&usb_audio_wr_frames, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&usb_audio_rd_frames, 0u, __ATOMIC_RELAXED);
    usb_audio_ring_window_reset(0u);
}

static size_t usb_audio_ring_push_i16(const int16_t *src_interleaved, size_t frames) {
    uint32_t wr = __atomic_load_n(&usb_audio_wr_frames, __ATOMIC_RELAXED);
    uint32_t rd = __atomic_load_n(&usb_audio_rd_frames, __ATOMIC_ACQUIRE);
    uint32_t fill = wr - rd;
    uint32_t free_frames = usb_audio_ring_free_frames(fill);

    size_t to_write = frames;
    if (to_write > free_frames) to_write = free_frames;

    for (size_t i = 0; i < to_write; i++) {
        uint32_t idx = (wr + (uint32_t)i) % USB_AUDIO_RING_FRAMES;
        usb_audio_ring[idx * 2 + 0] = src_interleaved[i * 2 + 0];
        usb_audio_ring[idx * 2 + 1] = src_interleaved[i * 2 + 1];
    }

    __atomic_store_n(&usb_audio_wr_frames, wr + (uint32_t)to_write, __ATOMIC_RELEASE);
    return to_write;
}

size_t usb_audio_pop_i16(int16_t *dst_interleaved_i16, size_t frames) {
    uint32_t wr = __atomic_load_n(&usb_audio_wr_frames, __ATOMIC_ACQUIRE);
    uint32_t rd = __atomic_load_n(&usb_audio_rd_frames, __ATOMIC_RELAXED);
    uint32_t avail = wr - rd;

    size_t to_read = frames;
    if (to_read > avail) to_read = avail;
    if (to_read < frames) {
        __atomic_fetch_add(&usb_audio_underrun_count, 1u, __ATOMIC_RELAXED);
    }

    for (size_t i = 0; i < to_read; i++) {
        uint32_t idx = (rd + (uint32_t)i) % USB_AUDIO_RING_FRAMES;
        dst_interleaved_i16[i * 2 + 0] = usb_audio_ring[idx * 2 + 0];
        dst_interleaved_i16[i * 2 + 1] = usb_audio_ring[idx * 2 + 1];
    }

    __atomic_store_n(&usb_audio_rd_frames, rd + (uint32_t)to_read, __ATOMIC_RELEASE);
    return to_read;
}

bool usb_audio_is_streaming(void) {
    return __atomic_load_n(&usb_audio_streaming, __ATOMIC_ACQUIRE) != 0u;
}

uint32_t usb_audio_get_sample_rate(void) {
    return USB_AUDIO_SAMPLE_RATE_HZ;
}

uint32_t usb_audio_get_last_avail_bytes(void) {
    return usb_audio_last_avail_bytes;
}

uint32_t usb_audio_get_last_rx_bytes(void) {
    return usb_audio_last_rx_bytes;
}

uint32_t usb_audio_get_ring_fill_frames(void) {
    return usb_audio_ring_fill_frames();
}

void usb_audio_take_ring_fill_window(uint32_t *out_min_frames, uint32_t *out_max_frames) {
    if (!out_min_frames || !out_max_frames) return;

    uint32_t current = usb_audio_ring_fill_frames();
    if (!usb_audio_ring_fill_window_valid) {
        usb_audio_ring_window_reset(current);
    }

    *out_min_frames = usb_audio_ring_fill_min_frames;
    *out_max_frames = usb_audio_ring_fill_max_frames;
    usb_audio_ring_window_reset(current);
}

uint32_t usb_audio_get_last_feedback_q16_16(void) {
    return usb_audio_last_fb_q16_16;
}

uint32_t usb_audio_get_feedback_update_count(void) {
    return usb_audio_fb_update_count;
}

uint32_t usb_audio_get_overrun_frames(void) {
    return usb_audio_overrun_frames;
}

uint32_t usb_audio_get_underrun_count(void) {
    return usb_audio_underrun_count;
}

uint32_t usb_audio_get_set_itf_count(void) {
    return usb_audio_set_itf_count;
}

uint8_t usb_audio_get_last_alt_setting(void) {
    return usb_audio_last_alt_setting;
}

uint8_t usb_audio_get_last_req_recipient(void) {
    return usb_audio_last_req_recipient;
}

uint8_t usb_audio_get_last_req_bRequest(void) {
    return usb_audio_last_req_bRequest;
}

uint8_t usb_audio_get_last_req_control_selector(void) {
    return usb_audio_last_req_control_selector;
}

uint8_t usb_audio_get_last_req_entity_id(void) {
    return usb_audio_last_req_entity_id;
}

uint16_t usb_audio_get_last_req_wLength(void) {
    return usb_audio_last_req_wLength;
}

uint32_t usb_audio_get_last_req_count(void) {
    return usb_audio_last_req_count;
}

void usb_audio_init(void) {
    usb_audio_ring_reset();
    __atomic_store_n(&usb_audio_streaming, 0u, __ATOMIC_RELEASE);
    usb_audio_feedback_reset_state();

    memset(mute, 0, sizeof(mute));
    memset(volume_q8_8, 0, sizeof(volume_q8_8));
    usb_audio_last_avail_bytes = 0u;
    usb_audio_last_rx_bytes = 0u;
    usb_audio_overrun_frames = 0u;
    usb_audio_underrun_count = 0u;
    usb_audio_set_itf_count = 0u;
    usb_audio_last_alt_setting = 0u;

    usb_audio_last_req_count = 0u;
    usb_audio_last_req_recipient = 0u;
    usb_audio_last_req_bRequest = 0u;
    usb_audio_last_req_control_selector = 0u;
    usb_audio_last_req_entity_id = 0u;
    usb_audio_last_req_wLength = 0u;
    current_latency = 0;

    usb_audio_last_fb_q16_16 = usb_audio_nominal_feedback_q16_16(USB_AUDIO_SAMPLE_RATE_HZ);
    usb_audio_fb_update_count = 0u;
    usb_audio_ring_fill_window_valid = false;
}

void usb_audio_task(void) {
    if (!tud_audio_mounted()) {
        usb_audio_last_avail_bytes = 0u;
        usb_audio_last_rx_bytes = 0u;
        usb_audio_ring_window_reset(0u);
        return;
    }

    uint32_t avail_bytes_now = tud_audio_available();
    usb_audio_last_avail_bytes = avail_bytes_now;

    // Most hosts only start sending audio after a non-zero alt setting is selected, but be tolerant:
    // if we see data in the FIFO, drain it even if our streaming flag hasn't been updated yet.
    if (!usb_audio_is_streaming() && avail_bytes_now == 0) {
        usb_audio_last_rx_bytes = 0u;
        return;
    }

    uint32_t fill = usb_audio_ring_fill_frames();
    usb_audio_ring_window_observe(fill);
    uint32_t free_frames = usb_audio_ring_free_frames(fill);
    if (free_frames == 0u) {
        usb_audio_last_rx_bytes = 0u;
        usb_audio_update_feedback();
        usb_audio_ring_window_observe(fill);
        return;
    }

    // Only PCM S16LE stereo at 48 kHz is supported in this firmware.
    const uint32_t bytes_per_frame = USB_AUDIO_BYTES_PER_FRAME;

    uint32_t to_read_bytes = avail_bytes_now;
    uint32_t free_bytes = free_frames * bytes_per_frame;
    if (to_read_bytes > free_bytes) to_read_bytes = free_bytes;

    // Keep frame alignment.
    to_read_bytes -= to_read_bytes % bytes_per_frame;
    if (to_read_bytes == 0) {
        usb_audio_last_rx_bytes = 0u;
        usb_audio_update_feedback();
        usb_audio_ring_window_observe(fill);
        return;
    }

    static int16_t pull_buf[USB_AUDIO_PULL_CHUNK_FRAMES * 2];
    const uint32_t pull_buf_bytes = (uint32_t)sizeof(pull_buf);

    uint32_t total_rx_bytes = 0;
    uint32_t pulled_chunks = 0u;
    while (to_read_bytes > 0 && pulled_chunks < (uint32_t)USB_AUDIO_MAX_PULL_CHUNKS_PER_TASK) {
        pulled_chunks++;
        uint32_t chunk_bytes = to_read_bytes;
        if (chunk_bytes > pull_buf_bytes) chunk_bytes = pull_buf_bytes;
        chunk_bytes -= chunk_bytes % bytes_per_frame;
        if (chunk_bytes == 0) break;

        uint16_t got_bytes = tud_audio_read(pull_buf, (uint16_t)chunk_bytes);
        got_bytes -= got_bytes % (uint16_t)bytes_per_frame;
        if (got_bytes == 0) break;

        total_rx_bytes += (uint32_t)got_bytes;

        size_t got_frames = (size_t)(got_bytes / bytes_per_frame);
        size_t pushed = usb_audio_ring_push_i16(pull_buf, got_frames);
        if (pushed == 0) break;

        if (pushed < got_frames) {
            usb_audio_overrun_frames += (uint32_t)(got_frames - pushed);
        }

        uint32_t pushed_bytes = (uint32_t)pushed * bytes_per_frame;
        if (pushed_bytes > to_read_bytes) break;
        to_read_bytes -= pushed_bytes;

        if (pushed < got_frames) {
            // Ring got full unexpectedly; stop draining.
            break;
        }
    }

    usb_audio_last_rx_bytes = total_rx_bytes;
    usb_audio_update_feedback();
    usb_audio_ring_window_observe(usb_audio_ring_fill_frames());
}

//--------------------------------------------------------------------+
// TinyUSB Audio callbacks (control + feedback)
//--------------------------------------------------------------------+

static void usb_audio_record_control_request(audio_control_request_t const *request) {
    usb_audio_last_req_count++;
    usb_audio_last_req_recipient = request->bmRequestType_bit.recipient;
    usb_audio_last_req_bRequest = request->bRequest;
    usb_audio_last_req_control_selector = request->bControlSelector;
    usb_audio_last_req_entity_id = request->bEntityID;
    usb_audio_last_req_wLength = request->wLength;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
    (void)func_id;
    (void)alt_itf;
    // Application-provided explicit feedback (usb_audio_update_feedback + tud_audio_fb_set).
    // This avoids coupling host feedback to TinyUSB's internal RX FIFO level; we instead
    // control the host rate based on the inter-core ring buffer fill.
    feedback_param->method = AUDIO_FEEDBACK_METHOD_DISABLED;
    feedback_param->sample_freq = usb_audio_get_sample_rate();
}

// Invoked when host selects alternate settings (start/stop streaming).
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;

    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

        if (itf == USB_AUDIO_ITF_STREAMING) {
            usb_audio_set_itf_count++;
            usb_audio_last_alt_setting = alt;
            if (alt != 0) {
                tud_audio_clear_ep_out_ff();
                usb_audio_ring_reset();
            __atomic_store_n(&usb_audio_streaming, 1u, __ATOMIC_RELEASE);
            usb_audio_feedback_reset_state();

            // Seed feedback so the endpoint starts sending immediately.
            uint32_t nominal = usb_audio_nominal_feedback_q16_16(usb_audio_get_sample_rate());
            (void)tud_audio_fb_set(nominal);
            usb_audio_last_fb_q16_16 = nominal;
            usb_audio_fb_update_count = 1u;
        } else {
                __atomic_store_n(&usb_audio_streaming, 0u, __ATOMIC_RELEASE);
                usb_audio_feedback_reset_state();
                tud_audio_clear_ep_out_ff();
                usb_audio_ring_reset();
            }
    }

    return true;
}

// Also called when an EP is closed (e.g., alt setting -> 0). Keep state consistent.
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;

    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    if (itf == USB_AUDIO_ITF_STREAMING && alt == 0) {
        usb_audio_last_alt_setting = 0u;
        __atomic_store_n(&usb_audio_streaming, 0u, __ATOMIC_RELEASE);
        usb_audio_hw_disable_endpoints();
        tud_audio_clear_ep_out_ff();
        usb_audio_ring_reset();
    }

    return true;
}

// Helper: clock source get requests (sample rate, validity).
static bool usb_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request) {
    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_4_t curf = { (int32_t)tu_htole32(usb_audio_get_sample_rate()) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
        }
        if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            audio_control_range_4_n_t(1) rangef = { 0 };
            rangef.wNumSubRanges = tu_htole16(1u);
            rangef.subrange[0].bMin = (int32_t)USB_AUDIO_SAMPLE_RATE_HZ;
            rangef.subrange[0].bMax = (int32_t)USB_AUDIO_SAMPLE_RATE_HZ;
            rangef.subrange[0].bRes = 0;
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
        }
    }

    if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t cur_valid = { .bCur = 1 };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
    }

    return false;
}

// Helper: clock source set requests (accept only 48 kHz).
static bool usb_audio_clock_set_request(audio_control_request_t const *request, uint8_t const *buf) {
    if (request->bControlSelector != AUDIO_CS_CTRL_SAM_FREQ) return false;
    if (request->bRequest != AUDIO_CS_REQ_CUR) return false;
    if (request->wLength != sizeof(audio_control_cur_4_t)) return false;

    uint32_t rate = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
    return (rate == USB_AUDIO_SAMPLE_RATE_HZ);
}

static bool usb_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request) {
    uint8_t ch = request->bChannelNumber;
    if (ch >= (uint8_t)TU_ARRAY_SIZE(mute)) ch = 0;

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t mute1 = { .bCur = mute[ch] };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
    }

    if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
        if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            // -50 dB .. 0 dB, 1 dB step (in 1/256 dB units)
            audio_control_range_2_n_t(1) range_vol = {
                .wNumSubRanges = tu_htole16(1),
                .subrange[0] = {
                    .bMin = tu_htole16((int16_t)(-50 * 256)),
                    .bMax = tu_htole16((int16_t)(0 * 256)),
                    .bRes = tu_htole16((int16_t)(1 * 256)),
                },
            };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
        }
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(volume_q8_8[ch]) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
        }
    }

    return false;
}

static bool usb_audio_feature_unit_set_request(audio_control_request_t const *request, uint8_t const *buf) {
    uint8_t ch = request->bChannelNumber;
    if (ch >= (uint8_t)TU_ARRAY_SIZE(mute)) ch = 0;

    if (request->bRequest != AUDIO_CS_REQ_CUR) return false;

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
        if (request->wLength != sizeof(audio_control_cur_1_t)) return false;
        mute[ch] = ((audio_control_cur_1_t const *)buf)->bCur ? 1 : 0;
        return true;
    }

    if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
        if (request->wLength != sizeof(audio_control_cur_2_t)) return false;
        volume_q8_8[ch] = ((audio_control_cur_2_t const *)buf)->bCur;
        return true;
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    usb_audio_record_control_request(request);

    if (request->bEntityID == USB_AUDIO_ENTITY_CLOCK) {
        return usb_audio_clock_get_request(rhport, request);
    }

    if (request->bEntityID == USB_AUDIO_ENTITY_FEATURE_UNIT) {
        return usb_audio_feature_unit_get_request(rhport, request);
    }

    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    (void)rhport;
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    usb_audio_record_control_request(request);

    if (request->bEntityID == USB_AUDIO_ENTITY_FEATURE_UNIT) {
        return usb_audio_feature_unit_set_request(request, buf);
    }

    if (request->bEntityID == USB_AUDIO_ENTITY_CLOCK) {
        return usb_audio_clock_set_request(request, buf);
    }

    return false;
}

// Interface-level AudioControl requests (e.g. Latency Control). Some hosts will probe these.
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    usb_audio_record_control_request(request);

    // Latency Control selector is 0x01 (UAC2 AudioControl interface). Use a 2-byte CUR value.
    if (request->bControlSelector == 0x01 && request->bRequest == AUDIO_CS_REQ_CUR &&
        request->wLength == sizeof(audio_control_cur_2_t)) {
        audio_control_cur_2_t cur_lat = { .bCur = tu_htole16(current_latency) };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_lat, sizeof(cur_lat));
    }

    // Default: respond with zeros to avoid stalling host probes.
    static uint8_t zeros[64];
    uint16_t len = request->wLength;
    if (len > (uint16_t)sizeof(zeros)) len = (uint16_t)sizeof(zeros);
    return tud_control_xfer(rhport, p_request, zeros, len);
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    (void)rhport;
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    usb_audio_record_control_request(request);

    if (request->bControlSelector == 0x01 && request->bRequest == AUDIO_CS_REQ_CUR &&
        request->wLength == sizeof(audio_control_cur_2_t)) {
        int16_t v = ((audio_control_cur_2_t const *)buf)->bCur;
        current_latency = v;
        return true;
    }

    // Accept unknown controls to avoid repeated host retries.
    return true;
}

// Endpoint-level requests are not used by our descriptors, but some hosts may still probe them.
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    usb_audio_record_control_request(request);

    static uint8_t zeros[64];
    uint16_t len = request->wLength;
    if (len > (uint16_t)sizeof(zeros)) len = (uint16_t)sizeof(zeros);
    return tud_control_xfer(rhport, p_request, zeros, len);
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    (void)rhport;
    (void)buf;
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    usb_audio_record_control_request(request);

    // Accept unknown controls to avoid repeated host retries.
    return true;
}
