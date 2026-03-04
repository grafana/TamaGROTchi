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
static char _otlp_traces_url[300];
static char _auth_b64[256];
static char _device_id[32];
static char _game_id[32];

// Grafana-themed random name generator — called once at init
// 45 prefixes × 45 suffixes = 2,025 combinations → ~9% collision chance with 20 instances
static void generate_game_id(char* buf, size_t len) {
    static const char* const PREFIXES[] = {
        "Loki", "Mimir", "Tempo", "Torkel", "Otel", "Prom",
        "Pyro", "Alloy", "Grot", "Beyla", "Faro", "Tanka",
        "Cortex", "Thanos", "Agent", "Oncall", "Sift", "Graph", "Panel", "Flux"
    };
    static const char* const SUFFIXES[] = {
        "tchi", "kins", "ito",  "bot",  "zu",  "mon", "ie",  "let", "y",
        "pup",  "pet",  "ling", "ster", "kun", "chan","tan",  "nu",  "mu",
        "chi",  "ko",   "ka",   "boo",  "moo", "doo", "pop",
        "pip",  "tip",  "nip",  "bug",  "mug", "pug",
        "wix",  "nix",  "mix",  "pix",  "zee", "bee",
        "coo",  "goo",  "loo",  "sox",  "fox", "box",
        "pod",  "nod"
    };
    // Use two independent random words to avoid correlation from bit-shifted single value
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint8_t pi  = r1 % (sizeof(PREFIXES) / sizeof(PREFIXES[0]));
    uint8_t si  = r2 % (sizeof(SUFFIXES) / sizeof(SUFFIXES[0]));
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
// Span (trace) buffer — protected by _log_mutex (shared)
// =============================================================================
static const uint8_t SPAN_BUF_CAP = 8;
struct SpanEntry {
    char     name[48];            // operation name e.g. "pet.fed"
    char     body[128];           // key=value detail string
    char     trace_id[33];        // 32 hex chars + null (pre-generated at buffer time)
    char     span_id[17];         // 16 hex chars + null
    char     parent_span_id[17];  // empty string = root span; otherwise parent's span_id
    uint64_t start_ns;
    uint32_t duration_ms;         // 0 → treated as 500
};
static SpanEntry _span_buf[SPAN_BUF_CAP];
static uint8_t   _span_count = 0;

// Active trace context — set by otlp_trace_begin(), cleared by otlp_trace_end().
// Only accessed from Core 1 (game loop); no mutex needed.
static char _active_trace_id[33]     = {};
static char _active_root_span_id[17] = {};

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

// Generate random hex ID: words×8 hex chars + null terminator.
// Caller must ensure buf is at least words*8+1 bytes.
//   trace ID: gen_hex_id(buf, 4) → 32 chars
//   span  ID: gen_hex_id(buf, 2) → 16 chars
static void gen_hex_id(char* buf, int words) {
    for (int i = 0; i < words; i++)
        snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
}

static void add_resource_attrs(JsonObject& resource) {
    JsonArray attrs = resource["attributes"].to<JsonArray>();
    auto add_attr = [&](const char* k, const char* v) {
        JsonObject a = attrs.add<JsonObject>();
        a["key"] = k;
        a["value"]["stringValue"] = v;
    };
    add_attr("service.name",      _game_id);
    add_attr("service.namespace", "tamagrotchi");
    add_attr("device",            _device_id);
}

static void add_gauge_metric(JsonArray& metrics, const char* name,
                             int64_t value, uint64_t ts_ns) {
    JsonObject m = metrics.add<JsonObject>();
    m["name"] = name;
    JsonArray dp = m["gauge"]["dataPoints"].to<JsonArray>();
    JsonObject d = dp.add<JsonObject>();
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
    add_attr("service.name",      _game_id);
    add_attr("service.namespace", "tamagrotchi");
    add_attr("device",            _device_id);
    add_attr("env",               "sciencefair");

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
// Trace (span) flush (called from background task only)
// Mirrors do_flush_logs(): snapshot under mutex, then send without holding it.
// Resource attributes differ from metrics/logs per OTLP tracing conventions:
//   service.name = game_id (e.g. "Lokitchi") — unique per game session
//   service.namespace = "tamagrotchi"
//   deployment.environment = "grafanacon"
// =============================================================================
static bool do_flush_traces() {
    SpanEntry snap[SPAN_BUF_CAP];
    uint8_t   count = 0;

    if (_log_mutex) {
        xSemaphoreTake(_log_mutex, portMAX_DELAY);
        count = _span_count;
        memcpy(snap, _span_buf, sizeof(SpanEntry) * count);
        _span_count = 0;
        xSemaphoreGive(_log_mutex);
    } else {
        count = _span_count;
        memcpy(snap, _span_buf, sizeof(SpanEntry) * count);
        _span_count = 0;
    }

    if (count == 0 || WiFi.status() != WL_CONNECTED) return true;

    JsonDocument doc;
    JsonArray rs = doc["resourceSpans"].to<JsonArray>();
    JsonObject r  = rs.add<JsonObject>();

    JsonObject resource = r["resource"].to<JsonObject>();
    JsonArray attrs = resource["attributes"].to<JsonArray>();
    auto add_attr = [&](const char* k, const char* v) {
        JsonObject a = attrs.add<JsonObject>();
        a["key"] = k;
        a["value"]["stringValue"] = v;
    };
    add_attr("service.name",           _game_id);
    add_attr("service.namespace",      "tamagrotchi");
    add_attr("deployment.environment", "grafanacon");

    JsonArray ss = r["scopeSpans"].to<JsonArray>();
    JsonObject scope = ss.add<JsonObject>();
    scope["scope"]["name"] = "tamagrotchi-firmware";
    JsonArray spans = scope["spans"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        const SpanEntry& se = snap[i];

        uint64_t start = se.start_ns;
        uint32_t dur   = (se.duration_ms == 0) ? 500 : se.duration_ms;
        uint64_t end   = start + (uint64_t)dur * 1000000ULL;

        char start_str[24], end_str[24];
        snprintf(start_str, sizeof(start_str), "%llu", (unsigned long long)start);
        snprintf(end_str,   sizeof(end_str),   "%llu", (unsigned long long)end);

        JsonObject sp = spans.add<JsonObject>();
        sp["traceId"]            = se.trace_id;
        sp["spanId"]             = se.span_id;
        if (se.parent_span_id[0] != '\0')
            sp["parentSpanId"]   = se.parent_span_id;
        sp["name"]               = se.name;
        sp["kind"]               = 1;   // INTERNAL
        sp["startTimeUnixNano"]  = start_str;
        sp["endTimeUnixNano"]    = end_str;
        sp["status"]["code"]     = 1;   // OK

        JsonArray sp_attrs = sp["attributes"].to<JsonArray>();
        JsonObject det = sp_attrs.add<JsonObject>();
        det["key"] = "event.details";
        det["value"]["stringValue"] = se.body;
    }

    String payload;
    serializeJson(doc, payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, _otlp_traces_url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", _auth_b64);
    int code = http.POST(payload);
    http.end();

    bool ok = (code >= 200 && code < 300);
    if (ok) Serial.printf("[otlp] traces flushed: %d spans, status=%d\n", count, code);
    else    Serial.printf("[otlp] traces FAIL: status=%d\n", code);
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
        bool t_ok = do_flush_traces();

        _last_push_ok  = m_ok && l_ok && t_ok;
        _push_complete = true;  // signal main loop to update UI
    }
}

// =============================================================================
// Public API
// =============================================================================
void otlp_writer_init(const char* otlp_base, const char* auth_b64, const char* device_id) {
    snprintf(_otlp_metrics_url, sizeof(_otlp_metrics_url), "%s/v1/metrics", otlp_base);
    snprintf(_otlp_logs_url,    sizeof(_otlp_logs_url),    "%s/v1/logs",    otlp_base);
    snprintf(_otlp_traces_url,  sizeof(_otlp_traces_url),  "%s/v1/traces",  otlp_base);
    strlcpy(_auth_b64,   auth_b64,   sizeof(_auth_b64));
    strlcpy(_device_id,  device_id,  sizeof(_device_id));
    generate_game_id(_game_id, sizeof(_game_id));
    Serial.printf("[otlp] metrics → %s\n", _otlp_metrics_url);
    Serial.printf("[otlp] logs    → %s\n", _otlp_logs_url);
    Serial.printf("[otlp] traces  → %s\n", _otlp_traces_url);
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

// Helper: write a SpanEntry into the buffer. Must hold _log_mutex on entry.
static void buffer_span(const char* name, const char* body, uint32_t duration_ms,
                        const char* trace_id, const char* span_id, const char* parent_span_id) {
    if (_span_count >= SPAN_BUF_CAP) {
        memmove(&_span_buf[0], &_span_buf[1], sizeof(SpanEntry) * (SPAN_BUF_CAP - 1));
        _span_count = SPAN_BUF_CAP - 1;
    }
    SpanEntry& s = _span_buf[_span_count++];
    strlcpy(s.name,           name,           sizeof(s.name));
    strlcpy(s.body,           body,           sizeof(s.body));
    strlcpy(s.trace_id,       trace_id,       sizeof(s.trace_id));
    strlcpy(s.span_id,        span_id,        sizeof(s.span_id));
    strlcpy(s.parent_span_id, parent_span_id, sizeof(s.parent_span_id));
    s.start_ns    = now_ns();
    s.duration_ms = duration_ms;
}

// Begin a new trace: generate trace_id + root span_id, store as active context,
// buffer the root span (no parent). Call from Core 1 game loop only.
void otlp_trace_begin(const char* name, const char* body, uint32_t duration_ms) {
    gen_hex_id(_active_trace_id,     4);   // 32-char trace ID
    gen_hex_id(_active_root_span_id, 2);   // 16-char root span ID

    if (_log_mutex) xSemaphoreTake(_log_mutex, portMAX_DELAY);
    buffer_span(name, body, duration_ms, _active_trace_id, _active_root_span_id, "");
    if (_log_mutex) xSemaphoreGive(_log_mutex);
}

// Clear active trace context. Call after the action completes.
void otlp_trace_end() {
    _active_trace_id[0]     = '\0';
    _active_root_span_id[0] = '\0';
}

// Buffer a child (or standalone) span (thread-safe).
// If an active trace exists (from otlp_trace_begin), attaches as a child of the root span.
// Otherwise creates an independent root span with its own trace ID.
// duration_ms=0 defaults to 500 ms at flush time.
void otlp_trace(const char* name, const char* body, uint32_t duration_ms) {
    // Generate IDs before taking mutex (esp_random is always safe)
    char new_span_id[17];
    gen_hex_id(new_span_id, 2);

    // Read active trace context (Core 1 only — no mutex needed for these variables)
    char tid[33], pid[17];
    if (_active_trace_id[0] != '\0') {
        strlcpy(tid, _active_trace_id,     sizeof(tid));
        strlcpy(pid, _active_root_span_id, sizeof(pid));
    } else {
        gen_hex_id(tid, 4);  // standalone span gets its own trace ID
        pid[0] = '\0';
    }

    if (_log_mutex) xSemaphoreTake(_log_mutex, portMAX_DELAY);
    buffer_span(name, body, duration_ms, tid, new_span_id, pid);
    if (_log_mutex) xSemaphoreGive(_log_mutex);
}
