#pragma once
#include <stdint.h>
#include <time.h>

// =============================================================================
// RTC Driver — PCF85063A via Wire (I2C 0x51)
// Provides hardware-backed time that survives power loss.
// NTP time (from configTime/SNTP) is written back to the RTC once synced.
// =============================================================================

void rtc_driver_init();

// Read the current RTC time into a `struct tm` (returns false if read fails).
bool rtc_driver_read(struct tm* out_tm);

// Write a time_t (epoch seconds, UTC) into the RTC.
// Call this once after NTP sync so the RTC stays accurate.
void rtc_driver_sync_from_ntp(time_t epoch_utc);

// Convenience: return the hour-of-day (0–23) from the RTC, or 12 on error.
uint8_t rtc_get_hour();
