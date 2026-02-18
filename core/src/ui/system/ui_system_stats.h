#ifndef UI_SYSTEM_STATS_H
#define UI_SYSTEM_STATS_H

#include <stdbool.h>
#include <stdint.h>

#include "input/ui_input.h"
#include "patch_api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct HeavyContextInterface;

typedef struct {
    uint8_t streaming;
    uint32_t ring_fill_frames;
    uint32_t ring_fill_min_frames;
    uint32_t ring_fill_max_frames;
    uint32_t last_avail_bytes;
    uint32_t last_rx_bytes;
    uint32_t underrun_count_total;
    uint32_t underrun_delta;
    uint32_t overrun_frames_total;
    uint32_t overrun_delta;
    uint32_t short_read_blocks_total;
    uint32_t short_read_blocks_delta;
} ui_system_usb_audio_stats_t;

typedef struct {
    uint32_t core1_dsp_avg_permille;
    uint32_t ram_heap_used_bytes;
    uint32_t ram_heap_free_bytes;
    uint32_t uptime_s;
    uint32_t cpu_mhz;
    uint32_t heavy_ctx_bytes;
    uint16_t heavy_in_count;
    ui_system_usb_audio_stats_t usb_audio;
    patch_api_transport_stats_t transport;
    ui_input_stats_t input;
} ui_system_stats_snapshot_t;

void ui_system_stats_init(struct HeavyContextInterface *ctx);
void ui_system_stats_poll(void);
bool ui_system_stats_get_snapshot(ui_system_stats_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* UI_SYSTEM_STATS_H */
