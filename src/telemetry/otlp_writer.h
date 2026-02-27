#pragma once
#include "../game/pet_state.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// OTLP Writer — sends metrics and logs to Grafana Cloud OTLP gateway
//
// Metrics: POST to <otlp_base>/v1/metrics  (Content-Type: application/json)
// Logs:    POST to <otlp_base>/v1/logs     (Content-Type: application/json)
//
// Pushes run on a background FreeRTOS task (Core 0) so the game loop
// is never blocked by network I/O.
// =============================================================================

// Call once in setup() with credentials from AppConfig.
void otlp_writer_init(const char* otlp_base,
                      const char* auth_b64,
                      const char* device_id);

// Call once in setup() after otlp_writer_init() to start the background task.
void otlp_writer_start_task();

// Non-blocking: snapshot pet state and signal the background task to push.
// Safe to call every loop() iteration at whatever cadence you like.
// If a push is already in flight this call is silently ignored.
void otlp_schedule_push(const PetState* p, float accel_mag, int rssi,
                        float battery_v);

// Returns true (once) when the most recent push has completed.
// Clears the flag on read — call once per loop() and act on the result.
bool otlp_push_complete();

// Returns true if the last completed push succeeded (HTTP 2xx).
bool otlp_last_push_ok();

// Buffer a log entry (up to 16 entries, thread-safe).
void otlp_log(uint8_t severity_number, const char* event, const char* body);
