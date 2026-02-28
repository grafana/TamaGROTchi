#include "wifi_manager.h"
#include "../config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <lwip/dns.h>

extern void game_log(uint8_t level, const char* event, const char* msg);

static WiFiState _state           = WiFiState::DISCONNECTED;
static uint32_t  _connectStartMs  = 0;
static uint32_t  _retryMs         = 0;
static char      _ssid[64]        = {};
static char      _pass[64]        = {};
static char      _ip_str[20]      = "0.0.0.0";
static bool      _ntp_synced      = false;

void wifi_manager_init(const char* ssid, const char* password) {
    strlcpy(_ssid, ssid,     sizeof(_ssid));
    strlcpy(_pass, password, sizeof(_pass));

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // keep radio on — modem sleep drops DNS responses
    WiFi.begin(_ssid, _pass);
    _connectStartMs = millis();
    _state = WiFiState::CONNECTING;
    Serial.printf("[wifi] Connecting to '%s'...\n", _ssid);
}

static void on_connected() {
    strlcpy(_ip_str, WiFi.localIP().toString().c_str(), sizeof(_ip_str));
    _state = WiFiState::CONNECTED;

    // Pin Google DNS into LWIP slot 1 as a fallback — DHCP primary in slot 0
    // is untouched. Prevents DNS going dark after reconnects or lease refresh.
    ip_addr_t fallback_dns;
    IP4_ADDR(&fallback_dns.u_addr.ip4, 8, 8, 8, 8);
    dns_setserver(1, &fallback_dns);

    char msg[100];
    snprintf(msg, sizeof(msg), "ssid=%s | rssi=%d | ip=%s",
             _ssid, WiFi.RSSI(), _ip_str);
    game_log(9 /*INFO*/, "wifi_connected", msg);
    Serial.printf("[wifi] Connected! IP=%s RSSI=%d\n", _ip_str, WiFi.RSSI());

    // Sync NTP once
    if (!_ntp_synced) {
        configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
        _ntp_synced = true;
        // NTP sync completes asynchronously; check in update()
    }
}

static void on_disconnected(const char* reason) {
    _state   = WiFiState::FAILED;
    _retryMs = millis();
    char msg[60];
    snprintf(msg, sizeof(msg), "reason=%s", reason);
    game_log(13 /*WARN*/, "wifi_disconnected", msg);
    Serial.printf("[wifi] Disconnected: %s. Retry in 30 s.\n", reason);
}

void wifi_manager_update() {
    uint32_t now = millis();

    switch (_state) {
    case WiFiState::CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
            on_connected();
        } else if ((now - _connectStartMs) >= WIFI_CONNECT_TIMEOUT_MS) {
            WiFi.disconnect();
            on_disconnected("timeout");
        }
        break;

    case WiFiState::CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
            on_disconnected("dropped");
            _state   = WiFiState::DISCONNECTED;
            _retryMs = 0;  // retry immediately
        } else {
            // Log NTP sync once system time is set
            if (_ntp_synced) {
                time_t t = time(nullptr);
                if (t > 1000000000L) {
                    // Time is valid — check once and then stop checking
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
        }
        break;

    case WiFiState::FAILED:
    case WiFiState::DISCONNECTED:
        if ((now - _retryMs) >= WIFI_RETRY_INTERVAL_MS) {
            WiFi.begin(_ssid, _pass);
            _connectStartMs = now;
            _state = WiFiState::CONNECTING;
            Serial.printf("[wifi] Retrying connection to '%s'...\n", _ssid);
        }
        break;
    }
}

WiFiState   wifi_manager_get_state()   { return _state; }
bool        wifi_manager_is_connected(){ return _state == WiFiState::CONNECTED; }
const char* wifi_manager_ip()          { return _ip_str; }
