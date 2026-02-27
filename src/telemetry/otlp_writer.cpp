#include "otlp_writer.h"
#include "../config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// =============================================================================
// Static config (written once at init, read-only after)
// =============================================================================
static char _otlp_metrics_url[300];
static char _otlp_logs_url[300];
static char _auth_b64[256];
static char _device_id[32];
static char _game_id[32];

// Grafana-themed random name generator — called once at init
static void generate_game_id(char* buf, size_t len) {
    static const char* const PREFIXES[] = {
        "Loki", "Mimir", "Tempo", "Torkel", "Otel", "Prom",
        "Pyro", "Alloy", "Grot", "Beyla", "Faro"
    };
    static const char* const SUFFIXES[] = {
        "tchi", "kins", "ito", "bot", "zu", "mon", "ie", "let", "y"
    };
    uint32_t r = esp_random();
    uint8_t pi = r % (sizeof(PREFIXES) / sizeof(PREFIXES[0]));
    uint8_t si = (r >> 8) % (sizeof(SUFFIXES) / sizeof(SUFFIXES[0]));
    snprintf(buf, len, "%s%s", PREFIXES[pi], SUFFIXES[si]);
}

// =============================================================================
// Log buffer — protected by _log_mutex
// =============================================================================
static const uint8_t LOG_BUF_CAP = 16;
struct LogEntry {
    uint8_t  severity;
    char     event[32];
    char     body[128];
    uint64_t ts_ns;
};
static LogEntry          _log_buf[LOG_BUF_CAP];
static uint8_t           _log_count = 0;
static SemaphoreHandle_t _log_mutex = nullptr;

// =============================================================================
// Background task state
// =============================================================================
struct TelemetrySnapshot {
    PetState pet;
    float    accel_mag;
    int      rssi;
    float    battery_v;
};

static SemaphoreHandle_t  _push_sem      = nullptr;  // binary: triggers push
static TelemetrySnapshot  _snapshot;                  // written before Give
static volatile bool      _push_complete = false;     // set by task, cleared by caller
static volatile bool      _last_push_ok  = false;     // result of last push

// =============================================================================
// Helpers
// =============================================================================
static uint64_t now_ns() {
    time_t t = time(nullptr);
    if (t < 1000000000L) t = 0;
    return (uint64_t)t * 1000000000ULL;
}

static void add_resource_attrs(JsonObject& resource) {
    JsonArray attrs = resource["attributes"].to<JsonArray>();
    auto add_attr = [&](const char* k, const char* v) {
        JsonObject a = attrs.add<JsonObject>();
        a["key"] = k;
        a["value"]["stringValue"] = v;
    };
    add_attr("service.name", "tamagrotchi");
    add_attr("device",       _device_id);
}

static void add_gauge_metric(JsonArray& metrics, const char* name,
                             int64_t value, uint64_t ts_ns) {
    JsonObject m = metrics.add<JsonObject>();
    m["name"] = name;
    JsonArray dp = m["gauge"]["dataPoints"].to<JsonArray>();
    JsonObject d = dp.add<JsonObject>();

    // game_id as a data point attribute → becomes a Prometheus label in Mimir
    JsonArray dp_attrs = d["attributes"].to<JsonArray>();
    JsonObject attr = dp_attrs.add<JsonObject>();
    attr["key"] = "game_id";
    attr["value"]["stringValue"] = _game_id;

    d["asInt"] = value;

    char ts_str[24];
    snprintf(ts_str, sizeof(ts_str), "%llu", (unsigned long long)ts_ns);
    d["timeUnixNano"] = ts_str;
}

// =============================================================================
// Metrics push (called from background task only)
// =============================================================================
static bool do_push_metrics(const PetState* p, float accel_mag, int rssi, float battery_v) {
    if (WiFi.status() != WL_CONNECTED) return false;

    uint64_t ts = now_ns();

    JsonDocument doc;
    JsonArray rm = doc["resourceMetrics"].to<JsonArray>();
    JsonObject r = rm.add<JsonObject>();

    JsonObject resource = r["resource"].to<JsonObject>();
    add_resource_attrs(resource);

    JsonArray sm = r["scopeMetrics"].to<JsonArray>();
    JsonObject scope = sm.add<JsonObject>();
    scope["scope"]["name"] = "tamagrotchi";

    JsonArray metrics = scope["metrics"].to<JsonArray>();
    add_gauge_metric(metrics, "tamagrotchi.stage",         (int64_t)p->stage,  ts);  // EGG=0…DEAD=6
    add_gauge_metric(metrics, "tamagrotchi.hunger",        p->hunger,          ts);
    add_gauge_metric(metrics, "tamagrotchi.happiness",     p->happiness,       ts);
    add_gauge_metric(metrics, "tamagrotchi.health",        p->health,          ts);
    add_gauge_metric(metrics, "tamagrotchi.age_s",         p->ageSeconds,      ts);
    add_gauge_metric(metrics, "tamagrotchi.care_mistakes", p->careMistakes,    ts);
    add_gauge_metric(metrics, "tamagrotchi.discipline",    p->disciplineScore, ts);
    add_gauge_metric(metrics, "tamagrotchi.wifi_rssi",     rssi,               ts);
    add_gauge_metric(metrics, "tamagrotchi.imu_accel_mag", (int64_t)(accel_mag * 100), ts);
    add_gauge_metric(metrics, "tamagrotchi.battery_mv",    (int64_t)(battery_v * 1000), ts);

    String payload;
    serializeJson(doc, payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, _otlp_metrics_url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", _auth_b64);
    int code = http.POST(payload);
    http.end();

    bool ok = (code >= 200 && code < 300);
    if (ok) Serial.printf("[otlp] metrics ok: %d\n", code);
    else    Serial.printf("[otlp] metrics FAIL: %d\n", code);
    return ok;
}

// =============================================================================
// Log flush (called from background task only)
// Snapshots the log buffer under the mutex, then sends without holding it.
// =============================================================================
static bool do_flush_logs() {
    // Snapshot under mutex to minimise hold time
    LogEntry snap[LOG_BUF_CAP];
    uint8_t  count = 0;

    if (_log_mutex) {
        xSemaphoreTake(_log_mutex, portMAX_DELAY);
        count = _log_count;
        memcpy(snap, _log_buf, sizeof(LogEntry) * count);
        _log_count = 0;
        xSemaphoreGive(_log_mutex);
    } else {
        count = _log_count;
        memcpy(snap, _log_buf, sizeof(LogEntry) * count);
        _log_count = 0;
    }

    if (count == 0 || WiFi.status() != WL_CONNECTED) return false;

    JsonDocument doc;
    JsonArray rl = doc["resourceLogs"].to<JsonArray>();
    JsonObject r  = rl.add<JsonObject>();

    JsonObject resource = r["resource"].to<JsonObject>();
    JsonArray attrs = resource["attributes"].to<JsonArray>();
    auto add_attr = [&](const char* k, const char* v) {
        JsonObject a = attrs.add<JsonObject>();
        a["key"] = k;
        a["value"]["stringValue"] = v;
    };
    add_attr("service.name", "tamagrotchi");
    add_attr("device",       _device_id);
    add_attr("game_id",      _game_id);
    add_attr("env",          "sciencefair");

    JsonArray sl = r["scopeLogs"].to<JsonArray>();
    JsonObject scope = sl.add<JsonObject>();
    JsonArray records = scope["logRecords"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        const LogEntry& le = snap[i];
        JsonObject rec = records.add<JsonObject>();

        char ts_str[24];
        snprintf(ts_str, sizeof(ts_str), "%llu", (unsigned long long)le.ts_ns);
        rec["timeUnixNano"]   = ts_str;
        rec["severityNumber"] = le.severity;

        const char* sev_text = "INFO";
        if      (le.severity <= 8)  sev_text = "DEBUG";
        else if (le.severity <= 12) sev_text = "INFO";
        else if (le.severity <= 16) sev_text = "WARN";
        else                        sev_text = "ERROR";
        rec["severityText"] = sev_text;

        rec["body"]["stringValue"] = le.body;

        JsonArray log_attrs = rec["attributes"].to<JsonArray>();
        JsonObject ev_attr = log_attrs.add<JsonObject>();
        ev_attr["key"] = "event";
        ev_attr["value"]["stringValue"] = le.event;
    }

    String payload;
    serializeJson(doc, payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, _otlp_logs_url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", _auth_b64);
    int code = http.POST(payload);
    http.end();

    bool ok = (code >= 200 && code < 300);
    if (ok) Serial.printf("[otlp] logs flushed: %d entries, status=%d\n", count, code);
    else    Serial.printf("[otlp] logs FAIL: status=%d\n", code);
    return ok;
}

// =============================================================================
// Background telemetry task — pinned to Core 0 (WiFi/protocol stack core)
// =============================================================================
static void telemetry_task(void*) {
    for (;;) {
        // Block until the main loop signals a push is due
        xSemaphoreTake(_push_sem, portMAX_DELAY);

        // Local copy of snapshot — safe because the semaphore Give on Core 1
        // acts as a memory barrier before the Take here on Core 0
        TelemetrySnapshot snap = _snapshot;

        bool m_ok = do_push_metrics(&snap.pet, snap.accel_mag, snap.rssi, snap.battery_v);
        bool l_ok = do_flush_logs();

        _last_push_ok  = m_ok;
        _push_complete = true;  // signal main loop to update UI
    }
}

// =============================================================================
// Public API
// =============================================================================
void otlp_writer_init(const char* otlp_base, const char* auth_b64, const char* device_id) {
    snprintf(_otlp_metrics_url, sizeof(_otlp_metrics_url), "%s/v1/metrics", otlp_base);
    snprintf(_otlp_logs_url,    sizeof(_otlp_logs_url),    "%s/v1/logs",    otlp_base);
    strlcpy(_auth_b64,   auth_b64,   sizeof(_auth_b64));
    strlcpy(_device_id,  device_id,  sizeof(_device_id));
    generate_game_id(_game_id, sizeof(_game_id));
    Serial.printf("[otlp] metrics → %s\n", _otlp_metrics_url);
    Serial.printf("[otlp] logs    → %s\n", _otlp_logs_url);
    Serial.printf("[otlp] game_id: %s\n",  _game_id);
}

void otlp_writer_start_task() {
    _push_sem  = xSemaphoreCreateBinary();
    _log_mutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(
        telemetry_task,
        "otlp_push",
        20480,    // 20 KB stack — SSL + JSON need headroom
        nullptr,
        1,        // priority 1 (low, below the Arduino loop task at 1)
        nullptr,
        0         // Core 0 — same core as WiFi stack
    );
    Serial.println("[otlp] background task started (Core 0)");
}

void otlp_schedule_push(const PetState* p, float accel_mag, int rssi, float battery_v) {
    if (!_push_sem) return;
    // Write snapshot before giving semaphore (memory barrier at Give)
    _snapshot.pet       = *p;
    _snapshot.accel_mag = accel_mag;
    _snapshot.rssi      = rssi;
    _snapshot.battery_v = battery_v;
    xSemaphoreGive(_push_sem);  // no-op if already given (push still in flight)
}

bool otlp_push_complete() {
    if (_push_complete) {
        _push_complete = false;
        return true;
    }
    return false;
}

bool otlp_last_push_ok() {
    return _last_push_ok;
}

void otlp_log(uint8_t severity_number, const char* event, const char* body) {
    if (_log_mutex) xSemaphoreTake(_log_mutex, portMAX_DELAY);

    if (_log_count >= LOG_BUF_CAP) {
        // Drop oldest entry
        memmove(&_log_buf[0], &_log_buf[1], sizeof(LogEntry) * (LOG_BUF_CAP - 1));
        _log_count = LOG_BUF_CAP - 1;
    }
    LogEntry& e = _log_buf[_log_count++];
    e.severity = severity_number;
    strlcpy(e.event, event, sizeof(e.event));
    strlcpy(e.body,  body,  sizeof(e.body));
    e.ts_ns    = now_ns();

    if (_log_mutex) xSemaphoreGive(_log_mutex);
}
