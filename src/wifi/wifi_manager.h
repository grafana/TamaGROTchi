#pragma once
#include <stdint.h>
#include <stdbool.h>

enum class WiFiState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING   = 1,
    CONNECTED    = 2,
    FAILED       = 3,
    ASLEEP       = 4,  // intentionally offline — do not auto-reconnect
};

// Call once in setup() — does NOT block; begins connecting asynchronously
void wifi_manager_init(const char* ssid, const char* password);

// Call every loop() — drives the non-blocking connection FSM
void wifi_manager_update();

WiFiState wifi_manager_get_state();
bool      wifi_manager_is_connected();

// IP address string (valid only when connected)
const char* wifi_manager_ip();

// Set state to ASLEEP and queue a deferred WiFi.disconnect() for Core 0.
// Returns immediately — radio goes down when wifi_manager_exec_pending() runs.
void wifi_manager_sleep();

// Set state to CONNECTING and queue a deferred WiFi.begin() for Core 0.
// Returns immediately — radio comes up when wifi_manager_exec_pending() runs.
void wifi_manager_wake();

// Execute any pending radio operation (disconnect/begin).
// MUST be called from Core 0 (telemetry task) only.
// Returns true if an operation was performed.
bool wifi_manager_exec_pending();
