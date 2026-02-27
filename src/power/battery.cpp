#include "battery.h"
#include "../config.h"
#include <Arduino.h>

void battery_init() {
    pinMode(PIN_BAT_ADC, INPUT);
    analogSetAttenuation(ADC_11db);  // 0–3.3V range
    analogReadResolution(12);        // 12-bit: 0–4095
}

float battery_read_voltage() {
    uint32_t sum = 0;
    for (int i = 0; i < BAT_ADC_SAMPLES; i++) {
        sum += analogRead(PIN_BAT_ADC);
    }
    float adc_avg = (float)sum / BAT_ADC_SAMPLES;
    float adc_v   = adc_avg / 4095.0f * 3.3f;
    return adc_v * BAT_DIVIDER_RATIO;
}

uint8_t battery_get_percent() {
    float v = battery_read_voltage();
    if (v >= BAT_V_FULL)  return 100;
    if (v <= BAT_V_EMPTY) return 0;
    return (uint8_t)((v - BAT_V_EMPTY) / (BAT_V_FULL - BAT_V_EMPTY) * 100.0f);
}
