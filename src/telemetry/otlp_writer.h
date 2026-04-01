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

// Returns the random Grafana-themed name generated at boot (e.g. "Lokitchi").
const char* otlp_get_game_id();

// Buffer a metric snapshot without WiFi (called every sample_interval_s).
// Flushed as a multi-point OTLP payload on the next push.
void otlp_sample_metrics(const PetState* p, float accel_mag, float battery_v);

// Buffer a log entry (up to 16 entries, thread-safe).
void otlp_log(uint8_t severity_number, const char* event, const char* body);

// Begin a new trace (call from Core 1 game loop before a user action).
// Generates a new trace ID + root span, stores as active context so that
// subsequent otlp_trace() calls during this action attach as child spans.
// duration_ms=0 → root span stored with 0 and treated as 500 ms at flush.
void otlp_trace_begin(const char* name, const char* body, uint32_t duration_ms = 0);

// Clear the active trace context (call after the action completes).
void otlp_trace_end();

// Buffer a span (up to 8, thread-safe). If an active trace exists (from
// otlp_trace_begin) this span is attached as a child of the root; otherwise
// it becomes a standalone root span with its own trace ID.
// duration_ms=0 defaults to 500 ms. Oldest span dropped when buffer is full.
void otlp_trace(const char* name, const char* body, uint32_t duration_ms = 500);
