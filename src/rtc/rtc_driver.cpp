#include "rtc_driver.h"
#include "../config.h"
#include <Arduino.h>
#include <Wire.h>

// =============================================================================
// PCF85063A register map (section 8.4 of the datasheet)
// =============================================================================
static constexpr uint8_t PCF_REG_CTRL1   = 0x00;
static constexpr uint8_t PCF_REG_SECONDS = 0x04;   // OS flag in bit 7

static uint8_t bcd_to_dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec_to_bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

static bool _ready = false;

// =============================================================================
// Init
// =============================================================================
void rtc_driver_init() {
    // Wire already started by imu_driver_init(); just probe the RTC.
    Wire.beginTransmission(RTC_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[rtc] PCF85063A not found");
        _ready = false;
        return;
    }

    // Clear the STOP bit (bit 5 of Control_1) to start the clock, in case
    // the oscillator was stopped.
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(PCF_REG_CTRL1);
    Wire.write(0x00);   // normal operation
    Wire.endTransmission();

    _ready = true;
    Serial.println("[rtc] PCF85063A ready");
}

// =============================================================================
// Read
// =============================================================================
bool rtc_driver_read(struct tm* out_tm) {
    if (!_ready) return false;

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(PCF_REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)7);
    if (Wire.available() < 7) return false;

    uint8_t sec  = Wire.read() & 0x7F;   // mask OS flag
    uint8_t min  = Wire.read() & 0x7F;
    uint8_t hour = Wire.read() & 0x3F;
    uint8_t day  = Wire.read() & 0x3F;
    /* weekday */ Wire.read();
    uint8_t mon  = Wire.read() & 0x1F;
    uint8_t year = Wire.read();

    out_tm->tm_sec   = bcd_to_dec(sec);
    out_tm->tm_min   = bcd_to_dec(min);
    out_tm->tm_hour  = bcd_to_dec(hour);
    out_tm->tm_mday  = bcd_to_dec(day);
    out_tm->tm_mon   = bcd_to_dec(mon) - 1;    // tm_mon is 0-based
    out_tm->tm_year  = bcd_to_dec(year) + 100; // PCF stores years since 2000; tm_year since 1900
    out_tm->tm_isdst = -1;
    mktime(out_tm);   // fill in tm_wday / tm_yday

    return true;
}

// =============================================================================
// Write NTP time back to RTC
// =============================================================================
void rtc_driver_sync_from_ntp(time_t epoch_utc) {
    if (!_ready) return;

    struct tm* t = gmtime(&epoch_utc);

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(PCF_REG_SECONDS);
    Wire.write(dec_to_bcd(t->tm_sec));
    Wire.write(dec_to_bcd(t->tm_min));
    Wire.write(dec_to_bcd(t->tm_hour));
    Wire.write(dec_to_bcd(t->tm_mday));
    Wire.write(dec_to_bcd(t->tm_wday));
    Wire.write(dec_to_bcd(t->tm_mon + 1));     // PCF months are 1-based
    Wire.write(dec_to_bcd(t->tm_year - 100));  // years since 2000
    Wire.endTransmission();

    Serial.printf("[rtc] Synced from NTP: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
                  t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                  t->tm_hour, t->tm_min, t->tm_sec);
}

// =============================================================================
// Convenience
// =============================================================================
uint8_t rtc_get_hour() {
    struct tm t = {};
    if (!rtc_driver_read(&t)) return 12;
    return (uint8_t)t.tm_hour;
}
