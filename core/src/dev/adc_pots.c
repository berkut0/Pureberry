/**
 * ADC potentiometers: init, read, 1-pole filter.
 * Config: first channel (POTS_ADC_FIRST_CHANNEL) and count (POTS_COUNT).
 * Physical pins are determined by the SDK: gpio = ADC_BASE_PIN + channel (chip-dependent).
 */

#include <stdio.h>
#include "adc_pots.h"
#include "config.h"
#include "hardware/adc.h"

#if (POTS_BACKEND == POTS_BACKEND_ADC)

#define ADC_MAX_RAW 4095.f

static float v_filtered[POTS_MAX];
static bool initialized;

bool adc_pots_init(void) {
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
    }
    initialized = true;
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

#else

bool adc_pots_init(void) {
    (void)0;
    return true;
}

void adc_pots_read(float *out, unsigned n) {
    (void)out;
    (void)n;
}

#endif /* POTS_BACKEND == POTS_BACKEND_ADC */
