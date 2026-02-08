/**
 * ADC potentiometers: init, read, 1-pole filter.
 * Config: first channel (POTS_ADC_FIRST_CHANNEL) and count (POTS_COUNT).
 * Physical pins are determined by the SDK: gpio = ADC_BASE_PIN + channel (chip-dependent).
 */

#include <stdio.h>
#include "adc_pots.h"
#include "config.h"
#include "patch_api.h"
#include "hardware/adc.h"
#include "pico/time.h"

#if (POTS_BACKEND == POTS_BACKEND_ADC)

#define ADC_MAX_RAW 4095.f

static float v_filtered[POTS_MAX];
static float pots_last_sent[POTS_MAX];
static uint32_t pots_last_poll_ms;
static bool initialized;
static bool enabled;

bool adc_pots_init(void) {
    enabled = false;
    initialized = false;
    if (POTS_COUNT == 0) return true;
    unsigned first = (unsigned)POTS_ADC_FIRST_CHANNEL;
    unsigned count = (unsigned)POTS_COUNT;
    unsigned max_user_ch = NUM_ADC_CHANNELS - 2u; /* last channel is temperature */
    if (first + count - 1u > max_user_ch) {
        printf("ADC pots init failed: channels %u..%u exceed max user channel %u\n",
               first, first + count - 1u, max_user_ch);
        return false;
    }
    adc_init();
    for (unsigned i = 0; i < count && i < (unsigned)POTS_MAX; i++) {
        unsigned ch = first + i;
        uint gpio = ADC_BASE_PIN + ch;
        adc_gpio_init(gpio);
        v_filtered[i] = 0.f;
        pots_last_sent[i] = -1.f;
    }
    pots_last_poll_ms = 0;
    initialized = true;
    enabled = true;
    return true;
}

void adc_pots_read(float *out, unsigned n) {
    if (!out || n == 0 || !initialized) return;
    if (n > (unsigned)POTS_COUNT) n = (unsigned)POTS_COUNT;
    if (n > (unsigned)POTS_MAX) n = (unsigned)POTS_MAX;

    unsigned first = (unsigned)POTS_ADC_FIRST_CHANNEL;
    float alpha = POTS_ALPHA;
    for (unsigned i = 0; i < n; i++) {
        unsigned ch = first + i;
        adc_select_input(ch);
        uint16_t raw = adc_read();
        float raw_norm = (float)raw / ADC_MAX_RAW;
        if (raw_norm < 0.f) raw_norm = 0.f;
        if (raw_norm > 1.f) raw_norm = 1.f;
        v_filtered[i] = v_filtered[i] + alpha * (raw_norm - v_filtered[i]);
        out[i] = v_filtered[i];
    }
}

void adc_pots_task(void) {
    if (!enabled) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - pots_last_poll_ms < (uint32_t)POTS_POLL_MS) return;
    pots_last_poll_ms = now;

    float values[POTS_MAX];
    adc_pots_read(values, (unsigned)POTS_COUNT);
    if (!initialized) return;

    for (unsigned i = 0; i < (unsigned)POTS_COUNT && i < (unsigned)POTS_MAX; i++) {
        float diff = values[i] - pots_last_sent[i];
        if (pots_last_sent[i] < 0.f || (diff > POTS_EPS || -diff > POTS_EPS)) {
            if (patch_api_push_knob((uint8_t)i, values[i]))
                pots_last_sent[i] = values[i];
            /* On ctrl_queue overflow (false): skip updating last_sent so the next poll may retry. */
        }
    }
}

#else

bool adc_pots_init(void) {
    (void)0;
    return true;
}

void adc_pots_task(void) {
    (void)0;
}

void adc_pots_read(float *out, unsigned n) {
    (void)out;
    (void)n;
}

#endif /* POTS_BACKEND == POTS_BACKEND_ADC */
