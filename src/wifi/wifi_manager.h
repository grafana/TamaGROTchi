#pragma once
#include <stdint.h>
#include <stdbool.h>

enum class WiFiState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING   = 1,
    CONNECTED    = 2,
    FAILED       = 3,
};

// Call once in setup() — does NOT block; begins connecting asynchronously
void wifi_manager_init(const char* ssid, const char* password);

// Call every loop() — drives the non-blocking connection FSM
void wifi_manager_update();

WiFiState wifi_manager_get_state();
bool      wifi_manager_is_connected();

// IP address string (valid only when connected)
const char* wifi_manager_ip();
