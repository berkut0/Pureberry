#include "ui/system/ui_system_stats.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "HvHeavy.h"
#include "audio_runtime.h"
#include "config.h"
#include "hardware/clocks.h"
#include "pico/time.h"
#ifdef ENABLE_USB_AUDIO
#include "usb/usb_audio.h"
#endif

static struct HeavyContextInterface *g_heavy_context;
static bool g_initialized;
static bool g_has_snapshot;
static absolute_time_t g_next_refresh;
static ui_system_stats_snapshot_t g_snapshot;
#ifdef ENABLE_USB_AUDIO
static uint32_t g_prev_usb_underrun_count;
static uint32_t g_prev_usb_overrun_frames;
static uint32_t g_prev_usb_short_read_blocks;
#endif

extern char end;
extern char __StackLimit;

static uint32_t clamp_u64_to_u32(uint64_t value) {
    if (value > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t) value;
}

static void ui_system_stats_fill_memory(ui_system_stats_snapshot_t *snapshot) {
    if (!snapshot) {
        return;
    }

    const uintptr_t heap_base = (uintptr_t) &end;
    const uintptr_t heap_limit = (uintptr_t) &__StackLimit;
    void *brk = sbrk(0);
    uintptr_t heap_top = heap_base;

    if (brk != (void *) -1) {
        heap_top = (uintptr_t) brk;
    }

    if (heap_top < heap_base) {
        heap_top = heap_base;
    }
    if (heap_top > heap_limit) {
        heap_top = heap_limit;
    }

    uint64_t used = 0u;
    uint64_t free_space = 0u;
    if (heap_top >= heap_base) {
        used = (uint64_t) (heap_top - heap_base);
    }
    if (heap_limit >= heap_top) {
        free_space = (uint64_t) (heap_limit - heap_top);
    }

    snapshot->ram_heap_used_bytes = clamp_u64_to_u32(used);
    snapshot->ram_heap_free_bytes = clamp_u64_to_u32(free_space);
}

void ui_system_stats_init(struct HeavyContextInterface *ctx) {
    g_heavy_context = ctx;
    g_initialized = true;
    g_has_snapshot = false;
    memset(&g_snapshot, 0, sizeof(g_snapshot));
#ifdef ENABLE_USB_AUDIO
    g_prev_usb_underrun_count = 0u;
    g_prev_usb_overrun_frames = 0u;
    g_prev_usb_short_read_blocks = 0u;
#endif
    g_next_refresh = get_absolute_time();
    ui_system_stats_poll();
}

void ui_system_stats_poll(void) {
    if (!g_initialized) {
        return;
    }

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, g_next_refresh) > 0) {
        return;
    }
    g_next_refresh = delayed_by_ms(now, UI_SYSTEM_STATS_REFRESH_MS);

    ui_system_stats_snapshot_t next = g_snapshot;
    next.core1_dsp_avg_permille = audio_runtime_get_core1_dsp_load_avg_permille();
    next.uptime_s = (uint32_t) (to_ms_since_boot(now) / 1000u);
    next.cpu_mhz = (uint32_t) (clock_get_hz(clk_sys) / 1000000u);
    next.heavy_ctx_bytes = g_heavy_context ? (uint32_t) hv_getSize(g_heavy_context) : 0u;
    next.heavy_in_count = patch_api_in_param_count();
#ifdef ENABLE_USB_AUDIO
    next.usb_audio.streaming = usb_audio_is_streaming() ? 1u : 0u;
    next.usb_audio.ring_fill_frames = usb_audio_get_ring_fill_frames();
    usb_audio_take_ring_fill_window(
        &next.usb_audio.ring_fill_min_frames,
        &next.usb_audio.ring_fill_max_frames
    );
    next.usb_audio.last_avail_bytes = usb_audio_get_last_avail_bytes();
    next.usb_audio.last_rx_bytes = usb_audio_get_last_rx_bytes();

    next.usb_audio.underrun_count_total = usb_audio_get_underrun_count();
    next.usb_audio.overrun_frames_total = usb_audio_get_overrun_frames();
    next.usb_audio.short_read_blocks_total = audio_runtime_get_core1_usb_short_read_blocks();

    next.usb_audio.underrun_delta =
        next.usb_audio.underrun_count_total - g_prev_usb_underrun_count;
    next.usb_audio.overrun_delta =
        next.usb_audio.overrun_frames_total - g_prev_usb_overrun_frames;
    next.usb_audio.short_read_blocks_delta =
        next.usb_audio.short_read_blocks_total - g_prev_usb_short_read_blocks;

    g_prev_usb_underrun_count = next.usb_audio.underrun_count_total;
    g_prev_usb_overrun_frames = next.usb_audio.overrun_frames_total;
    g_prev_usb_short_read_blocks = next.usb_audio.short_read_blocks_total;
#else
    memset(&next.usb_audio, 0, sizeof(next.usb_audio));
#endif

    ui_system_stats_fill_memory(&next);
    patch_api_get_transport_stats(&next.transport);
    ui_input_get_stats(&next.input);

    g_snapshot = next;
    g_has_snapshot = true;
}

bool ui_system_stats_get_snapshot(ui_system_stats_snapshot_t *out_snapshot) {
    if (!out_snapshot || !g_initialized) {
        return false;
    }

    ui_system_stats_poll();
    if (!g_has_snapshot) {
        return false;
    }

    *out_snapshot = g_snapshot;
    return true;
}
