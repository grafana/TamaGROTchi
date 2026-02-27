#pragma once
#include <stdint.h>

// =============================================================================
// IMU Driver — QMI8658C via FastIMU
// Shake detection with two severity levels.
// =============================================================================

enum class ShakeLevel : uint8_t { NONE, GENTLE, HARD };

void       imu_driver_init();
void       imu_driver_update();    // call ~10 Hz from loop()
ShakeLevel imu_get_shake();        // returns and clears the current shake event
float      imu_get_accel_mag();    // latest |accel| in g (for OTLP metric)
