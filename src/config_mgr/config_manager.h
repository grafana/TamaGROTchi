#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// AppConfig — loaded from /config.json on LittleFS at boot.
// Falls back to compile-time defaults from config.h if file missing/invalid.
// =============================================================================

struct AppConfig {
    char wifi_ssid[64];
    char wifi_password[64];
    char otlp_base[256];   // e.g. "https://otlp-gateway-prod-eu-west-0.grafana.net/otlp"
    char auth_b64[256];    // pre-encoded "Basic <base64(instanceId:apiKey)>"
    char device_id[32];    // used as OTLP resource attribute
    bool demo_speed;           // true = fast evolution for demo
    uint32_t push_interval_s;  // telemetry push cadence in seconds
    bool buzzer_enabled;       // false = mute all sounds
    bool flash_alerts;         // true = flash backlight whenever a melody plays
};

// Load config from LittleFS /config.json.
// Returns true if config.json was found and parsed successfully.
// Always populates cfg — uses compile-time defaults for any missing fields.
bool config_manager_load(AppConfig* cfg);

// Print config to Serial (hides password/auth_b64)
void config_manager_print(const AppConfig* cfg);
