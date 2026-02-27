#include "imu_driver.h"
#include "../config.h"
#include <Arduino.h>
#include <Wire.h>
#include <FastIMU.h>   // umbrella header — includes F_QMI8658.hpp

// =============================================================================
// State
// =============================================================================
static QMI8658   _imu;
static bool      _ready        = false;
static float     _accel_mag    = 1.0f;   // last measured magnitude (g)
static ShakeLevel _pending     = ShakeLevel::NONE;

// Shake sustain tracking
static float    _shake_peak_g  = 0.0f;
static uint32_t _shake_start_ms = 0;
static bool     _in_shake      = false;

// =============================================================================
// Init
// =============================================================================
void imu_driver_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);

    calData calib = {};   // no calibration offsets
    int err = _imu.init(calib, IMU_I2C_ADDR);
    if (err != 0) {
        Serial.printf("[imu] QMI8658 init FAILED (err=%d)\n", err);
        _ready = false;
        return;
    }
    _ready = true;
    Serial.println("[imu] QMI8658 ready");
}

// =============================================================================
// Update  (call ~10 Hz)
// =============================================================================
void imu_driver_update() {
    if (!_ready) return;

    AccelData a;
    _imu.update();
    _imu.getAccel(&a);

    float mag = sqrtf(a.accelX * a.accelX +
                      a.accelY * a.accelY +
                      a.accelZ * a.accelZ);
    _accel_mag = mag;

    uint32_t now = millis();

    if (mag >= SHAKE_HARD_G) {
        // Hard shake zone
        if (!_in_shake) {
            _in_shake      = true;
            _shake_start_ms = now;
            _shake_peak_g  = mag;
        } else {
            if (mag > _shake_peak_g) _shake_peak_g = mag;
            if ((now - _shake_start_ms) >= SHAKE_HARD_MS &&
                _pending == ShakeLevel::NONE) {
                _pending = ShakeLevel::HARD;
            }
        }
    } else if (mag >= SHAKE_GENTLE_G) {
        // Gentle shake zone
        if (!_in_shake) {
            _in_shake      = true;
            _shake_start_ms = now;
            _shake_peak_g  = mag;
        } else {
            if (mag > _shake_peak_g) _shake_peak_g = mag;
            if ((now - _shake_start_ms) >= SHAKE_GENTLE_MS &&
                _pending == ShakeLevel::NONE) {
                _pending = ShakeLevel::GENTLE;
            }
        }
    } else {
        // Below threshold — reset detector
        _in_shake     = false;
        _shake_peak_g = 0.0f;
    }
}

// =============================================================================
// Accessors
// =============================================================================
ShakeLevel imu_get_shake() {
    ShakeLevel s = _pending;
    _pending = ShakeLevel::NONE;
    return s;
}

float imu_get_accel_mag() {
    return _accel_mag;
}
