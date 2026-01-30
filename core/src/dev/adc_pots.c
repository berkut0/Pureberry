/**
 * ADC potentiometers: init, read, 1-pole filter. GPIO 26-29 → ADC0-3.
 * Contract: config provides GPIO pins; we map to adc_input = gpio - 26 and validate 26..29.
 */

#include "adc_pots.h"
#include "config.h"
#include "hardware/adc.h"

#if (POTS_BACKEND == POTS_BACKEND_ADC)

#define POTS_ADC_FIRST_GPIO 26
#define POTS_ADC_LAST_GPIO  29
#define ADC_MAX_RAW 4095.f

static const unsigned int pots_gpio[4] = {
    POTS_ADC_GPIO_0,
    POTS_ADC_GPIO_1,
    POTS_ADC_GPIO_2,
    POTS_ADC_GPIO_3,
};

static float v_filtered[4];
static bool initialized;

static inline unsigned gpio_to_adc_input(unsigned int gpio) {
    if (gpio < POTS_ADC_FIRST_GPIO || gpio > POTS_ADC_LAST_GPIO)
        return (unsigned)-1;
    return gpio - POTS_ADC_FIRST_GPIO;
}

bool adc_pots_init(void) {
    if (POTS_COUNT == 0) return true;
    adc_init();
    for (unsigned i = 0; i < (unsigned)POTS_COUNT && i < 4u; i++) {
        unsigned int gpio = pots_gpio[i];
        unsigned ch = gpio_to_adc_input(gpio);
        if (ch == (unsigned)-1) continue;
        adc_gpio_init(gpio);
        v_filtered[i] = 0.f;
    }
    initialized = true;
    return true;
}

void adc_pots_read(float *out, unsigned n) {
    if (!out || n == 0 || !initialized) return;
    if (n > (unsigned)POTS_COUNT) n = (unsigned)POTS_COUNT;
    if (n > 4u) n = 4u;

    float alpha = POTS_ALPHA;
    for (unsigned i = 0; i < n; i++) {
        unsigned int gpio = pots_gpio[i];
        unsigned ch = gpio_to_adc_input(gpio);
        float raw_norm = 0.f;
        if (ch != (unsigned)-1) {
            adc_select_input(ch);
            uint16_t raw = adc_read();
            raw_norm = (float)raw / ADC_MAX_RAW;
            if (raw_norm < 0.f) raw_norm = 0.f;
            if (raw_norm > 1.f) raw_norm = 1.f;
        }
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
