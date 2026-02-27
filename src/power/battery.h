#pragma once
#include <stdint.h>

// =============================================================================
// Battery driver — GPIO1 BAT ADC (200kΩ/100kΩ voltage divider)
//
// Voltage divider scales battery voltage (3.0–4.2V) to ADC range (1.0–1.4V).
// 16 samples are averaged to reduce ADC noise.
// =============================================================================

// Call once in setup() — configures ADC attenuation and resolution.
void battery_init();

// Read battery voltage in volts (e.g. 3.7). Averages BAT_ADC_SAMPLES readings.
float battery_read_voltage();

// Returns battery level as 0–100, clamped. Based on 3.0V=0% / 4.2V=100%.
uint8_t battery_get_percent();
