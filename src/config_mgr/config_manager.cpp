#include "config_manager.h"
#include "../config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static void apply_defaults(AppConfig* cfg) {
    strlcpy(cfg->wifi_ssid,       WIFI_SSID_DEFAULT,    sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_password,   WIFI_PASS_DEFAULT,    sizeof(cfg->wifi_password));
    strlcpy(cfg->otlp_base,       OTLP_BASE_URL_DEFAULT, sizeof(cfg->otlp_base));
    strlcpy(cfg->auth_b64,        OTLP_AUTH_DEFAULT,    sizeof(cfg->auth_b64));
    strlcpy(cfg->device_id,       OTLP_DEVICE_ID_DEFAULT, sizeof(cfg->device_id));
    cfg->demo_speed         = true;
    cfg->push_interval_s    = TELEMETRY_PUSH_INTERVAL_MS / 1000;
    cfg->sample_interval_s  = TELEMETRY_SAMPLE_INTERVAL_MS / 1000;
    cfg->buzzer_enabled   = true;
    cfg->flash_alerts     = true;
}

bool config_manager_load(AppConfig* cfg) {
    apply_defaults(cfg);

    // Partition label must match partitions_16M.csv name ("littlefs", not "spiffs")
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.println("[config] LittleFS mount failed — using defaults");
        return false;
    }

    if (!LittleFS.exists("/config.json")) {
        Serial.println("[config] /config.json not found — using defaults");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[config] Failed to open /config.json — using defaults");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[config] JSON parse error: %s — using defaults\n", err.c_str());
        return false;
    }

    // WiFi
    if (doc["wifi"]["ssid"].is<const char*>())
        strlcpy(cfg->wifi_ssid, doc["wifi"]["ssid"], sizeof(cfg->wifi_ssid));
    if (doc["wifi"]["password"].is<const char*>())
        strlcpy(cfg->wifi_password, doc["wifi"]["password"], sizeof(cfg->wifi_password));

    // Grafana
    if (doc["grafana"]["otlp_base"].is<const char*>())
        strlcpy(cfg->otlp_base, doc["grafana"]["otlp_base"], sizeof(cfg->otlp_base));
    if (doc["grafana"]["auth_b64"].is<const char*>())
        strlcpy(cfg->auth_b64, doc["grafana"]["auth_b64"], sizeof(cfg->auth_b64));
    if (doc["grafana"]["device_id"].is<const char*>())
        strlcpy(cfg->device_id, doc["grafana"]["device_id"], sizeof(cfg->device_id));

    // Game
    if (doc["game"]["demo_speed"].is<bool>())
        cfg->demo_speed = doc["game"]["demo_speed"];
    if (doc["game"]["push_interval_s"].is<uint32_t>())
        cfg->push_interval_s = doc["game"]["push_interval_s"];
    if (doc["game"]["sample_interval_s"].is<uint32_t>())
        cfg->sample_interval_s = doc["game"]["sample_interval_s"];
    if (doc["game"]["buzzer"].is<bool>())
        cfg->buzzer_enabled = doc["game"]["buzzer"];
    if (doc["game"]["flash_alerts"].is<bool>())
        cfg->flash_alerts = doc["game"]["flash_alerts"];

    Serial.println("[config] Loaded from /config.json");
    return true;
}

void config_manager_print(const AppConfig* cfg) {
    Serial.printf("[config] wifi_ssid:      %s\n", cfg->wifi_ssid);
    Serial.printf("[config] otlp_base:      %s\n", cfg->otlp_base);
    Serial.printf("[config] device_id:      %s\n", cfg->device_id);
    Serial.printf("[config] demo_speed:     %s\n", cfg->demo_speed ? "true" : "false");
    Serial.printf("[config] push_interval:   %lu s\n", cfg->push_interval_s);
    Serial.printf("[config] sample_interval: %lu s\n", cfg->sample_interval_s);
    Serial.printf("[config] buzzer:         %s\n", cfg->buzzer_enabled ? "on" : "muted");
    Serial.printf("[config] flash_alerts:   %s\n", cfg->flash_alerts ? "on" : "off");
    Serial.printf("[config] auth_b64:       %.6s... (len=%d)\n",
                  cfg->auth_b64, (int)strlen(cfg->auth_b64));
}
