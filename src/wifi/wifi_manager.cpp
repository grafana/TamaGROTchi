#include "wifi_manager.h"
#include "../config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <lwip/dns.h>

extern void game_log(uint8_t level, const char* event, const char* msg);

static volatile WiFiState _state         = WiFiState::DISCONNECTED;
static uint32_t  _connectStartMs         = 0;
static uint32_t  _retryMs                = 0;
static char      _ssid[64]               = {};
static char      _pass[64]               = {};
static char      _ip_str[20]             = "0.0.0.0";
static bool      _ntp_synced             = false;
static bool      _intentionally_asleep   = false;
static volatile uint8_t _pending_op      = 0;
static const uint8_t    OP_SLEEP         = 1;
static const uint8_t    OP_WAKE          = 2;
static const uint8_t    OP_TIMEOUT       = 3;  // connect timed out → WiFi.disconnect()

// =============================================================================
// Internal helpers — called from Core 1 only
// =============================================================================
static void on_connected() {
    strlcpy(_ip_str, WiFi.localIP().toString().c_str(), sizeof(_ip_str));
    _state = WiFiState::CONNECTED;

    // Pin Google DNS as LWIP slot 1 fallback — prevents DNS going dark after reconnects.
    ip_addr_t fallback_dns;
    IP4_ADDR(&fallback_dns.u_addr.ip4, 8, 8, 8, 8);
    dns_setserver(1, &fallback_dns);

    char msg[100];
    snprintf(msg, sizeof(msg), "ssid=%s | rssi=%d | ip=%s",
             _ssid, WiFi.RSSI(), _ip_str);
    game_log(9 /*INFO*/, "wifi_connected", msg);
    Serial.printf("[wifi] Connected! IP=%s RSSI=%d\n", _ip_str, WiFi.RSSI());

    if (!_ntp_synced) {
        configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
        _ntp_synced = true;
    }
}

static void on_disconnected(const char* reason) {
    _state   = WiFiState::FAILED;
    _retryMs = millis();
    char msg[60];
    snprintf(msg, sizeof(msg), "reason=%s", reason);
    game_log(13 /*WARN*/, "wifi_disconnected", msg);
    Serial.printf("[wifi] Disconnected: %s\n", reason);
}

// =============================================================================
// WiFi event callback — runs on Core 0 (WiFi event task).
// Only touches volatile state; no mutex, no Serial, no game_log here.
// on_connected/on_disconnected are called from Core 1 in wifi_manager_update().
// =============================================================================
static volatile bool _ev_got_ip      = false;
static volatile bool _ev_disconnected = false;

static void wifi_event_handler(WiFiEvent_t event) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        _ev_got_ip = true;
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (!_intentionally_asleep)
            _ev_disconnected = true;
        break;
    default:
        break;
    }
}

// =============================================================================
// Public API
// =============================================================================
void wifi_manager_init(const char* ssid, const char* password) {
    strlcpy(_ssid, ssid,     sizeof(_ssid));
    strlcpy(_pass, password, sizeof(_pass));

    WiFi.mode(WIFI_STA);
    // WIFI_PS_MIN_MODEM lets the radio sleep between DTIM beacon intervals.
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    WiFi.onEvent(wifi_event_handler);
    WiFi.begin(_ssid, _pass);
    _connectStartMs = millis();
    _state = WiFiState::CONNECTING;
    Serial.printf("[wifi] Connecting to '%s'...\n", _ssid);
}

void wifi_manager_update() {
    // Consume events set by Core 0 callback — no WiFi API calls here.
    if (_ev_got_ip) {
        _ev_got_ip = false;
        if (_state == WiFiState::CONNECTING)
            on_connected();
    }
    if (_ev_disconnected) {
        _ev_disconnected = false;
        if (_state == WiFiState::CONNECTED || _state == WiFiState::CONNECTING)
            on_disconnected("dropped");
    }

    uint32_t now = millis();

    switch (_state) {
    case WiFiState::CONNECTING:
        if ((now - _connectStartMs) >= WIFI_CONNECT_TIMEOUT_MS) {
            on_disconnected("timeout");
            _pending_op = OP_TIMEOUT;  // Core 0 will call WiFi.disconnect()
        }
        break;

    case WiFiState::CONNECTED:
        // Log NTP sync once system time is set
        if (_ntp_synced) {
            time_t t = time(nullptr);
            if (t > 1000000000L) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    char tbuf[32];
                    const struct tm* ti = localtime(&t);
                    strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", ti);
                    char msg[60];
                    snprintf(msg, sizeof(msg), "time=%s", tbuf);
                    game_log(9 /*INFO*/, "ntp_synced", msg);
                }
            }
        }
        break;

    case WiFiState::FAILED:
    case WiFiState::DISCONNECTED:
        if (_intentionally_asleep) break;
        if ((now - _retryMs) >= WIFI_RETRY_INTERVAL_MS)
            wifi_manager_wake();
        break;

    case WiFiState::ASLEEP:
        break;
    }
}

void wifi_manager_sleep() {
    _intentionally_asleep = true;
    _state    = WiFiState::ASLEEP;
    _pending_op = OP_SLEEP;  // Core 0 will call WiFi.disconnect()
    Serial.println("[wifi] Sleeping (deferred radio off)");
}

void wifi_manager_wake() {
    if (_state == WiFiState::CONNECTED) return;
    _intentionally_asleep = false;
    _connectStartMs = millis();
    _state    = WiFiState::CONNECTING;
    _pending_op = OP_WAKE;  // Core 0 will call WiFi.begin()
    Serial.printf("[wifi] Waking (deferred radio on)...\n");
}

bool wifi_manager_exec_pending() {
    uint8_t op = _pending_op;
    if (op == 0) return false;
    _pending_op = 0;
    switch (op) {
    case OP_SLEEP:
    case OP_TIMEOUT:
        WiFi.disconnect(false);
        Serial.println("[wifi] Radio off");
        break;
    case OP_WAKE:
        WiFi.begin(_ssid, _pass);
        Serial.printf("[wifi] Radio connecting to '%s'...\n", _ssid);
        break;
    }
    return true;
}

WiFiState   wifi_manager_get_state()    { return _state; }
bool        wifi_manager_is_connected() { return _state == WiFiState::CONNECTED; }
const char* wifi_manager_ip()           { return _ip_str; }
