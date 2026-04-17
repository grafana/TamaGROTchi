"""OTLP HTTP/JSON client — output format identical to otlp_writer.cpp.

Pushes metrics, logs, and traces to Grafana Cloud OTLP gateway.
All three endpoints share the same base URL and auth header.
"""

from __future__ import annotations

import secrets
import threading
import time
from dataclasses import dataclass, field
from typing import Any

import requests

# ---------------------------------------------------------------------------
# Internal buffer types
# ---------------------------------------------------------------------------

@dataclass
class _LogEntry:
    severity: int          # 9=INFO, 13=WARN, 17=ERROR
    event:    str
    body:     str
    ts_ns:    int


@dataclass
class _SpanEntry:
    name:           str
    body:           str
    trace_id:       str    # 32 hex chars
    span_id:        str    # 16 hex chars
    parent_span_id: str    # empty = root
    start_ns:       int
    duration_ms:    int    # 0 → treated as 500 at flush


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _now_ns() -> int:
    return int(time.time() * 1_000_000_000)


def _gen_trace_id() -> str:
    return secrets.token_hex(16)   # 32 hex chars


def _gen_span_id() -> str:
    return secrets.token_hex(8)    # 16 hex chars


def _sev_text(severity: int) -> str:
    if severity <= 8:
        return "DEBUG"
    if severity <= 12:
        return "INFO"
    if severity <= 16:
        return "WARN"
    return "ERROR"


# ---------------------------------------------------------------------------
# OtlpClient
# ---------------------------------------------------------------------------

LOG_BUF_CAP  = 16
SPAN_BUF_CAP = 8


class OtlpClient:
    """Thread-safe OTLP client for one TamaGROTchi instance."""

    def __init__(
        self,
        otlp_base:  str,
        auth_b64:   str,
        device_id:  str,
        game_id:    str,
        verbose:    bool = False,
        k8s_attrs:  dict[str, str] | None = None,
    ) -> None:
        self._metrics_url = f"{otlp_base.rstrip('/')}/v1/metrics"
        self._logs_url    = f"{otlp_base.rstrip('/')}/v1/logs"
        self._traces_url  = f"{otlp_base.rstrip('/')}/v1/traces"
        self._auth        = auth_b64
        self._device_id   = device_id
        self._game_id     = game_id
        self._verbose     = verbose
        self._k8s_attrs   = k8s_attrs or {}

        self._lock:      threading.Lock   = threading.Lock()
        self._log_buf:   list[_LogEntry]  = []
        self._span_buf:  list[_SpanEntry] = []

        # Active trace context (single-threaded game loop — no lock needed)
        self._active_trace_id:     str = ""
        self._active_root_span_id: str = ""

        self._session = requests.Session()
        self._session.headers.update({
            "Content-Type": "application/json",
            "Authorization": self._auth,
        })

    # ------------------------------------------------------------------
    # Resource attributes (common to all three signals)
    # ------------------------------------------------------------------

    def _resource_attrs(self, extra: dict[str, str] | None = None) -> list[dict]:
        attrs = [
            {"key": "service.name",        "value": {"stringValue": self._game_id}},
            {"key": "service.namespace",   "value": {"stringValue": "tamagrotchi"}},
            {"key": "service.instance.id", "value": {"stringValue": self._device_id}},
            {"key": "host.name",           "value": {"stringValue": self._device_id}},
        ]
        for k, v in self._k8s_attrs.items():
            attrs.append({"key": k, "value": {"stringValue": v}})
        if extra:
            for k, v in extra.items():
                attrs.append({"key": k, "value": {"stringValue": v}})
        return attrs

    # ------------------------------------------------------------------
    # Log buffering
    # ------------------------------------------------------------------

    def log(self, severity: int, event: str, body: str) -> None:
        """Buffer a log entry (mirrors otlp_log())."""
        entry = _LogEntry(severity=severity, event=event, body=body, ts_ns=_now_ns())
        with self._lock:
            if len(self._log_buf) >= LOG_BUF_CAP:
                self._log_buf.pop(0)   # drop oldest
            self._log_buf.append(entry)

    # ------------------------------------------------------------------
    # Span buffering
    # ------------------------------------------------------------------

    def _buffer_span(
        self,
        name:           str,
        body:           str,
        duration_ms:    int,
        trace_id:       str,
        span_id:        str,
        parent_span_id: str,
    ) -> None:
        """Write a SpanEntry into the buffer (must hold _lock on entry or be called internally)."""
        entry = _SpanEntry(
            name=name,
            body=body,
            trace_id=trace_id,
            span_id=span_id,
            parent_span_id=parent_span_id,
            start_ns=_now_ns(),
            duration_ms=duration_ms,
        )
        with self._lock:
            if len(self._span_buf) >= SPAN_BUF_CAP:
                self._span_buf.pop(0)
            self._span_buf.append(entry)

    def trace_begin(self, name: str, body: str, duration_ms: int = 0) -> None:
        """Start a new trace: generates trace_id + root span_id, buffers root span."""
        self._active_trace_id     = _gen_trace_id()
        self._active_root_span_id = _gen_span_id()
        self._buffer_span(
            name, body, duration_ms,
            self._active_trace_id,
            self._active_root_span_id,
            "",
        )

    def trace_end(self) -> None:
        """Clear active trace context."""
        self._active_trace_id     = ""
        self._active_root_span_id = ""

    def trace(self, name: str, body: str, duration_ms: int = 500) -> None:
        """Buffer a child (or standalone) span (mirrors otlp_trace())."""
        new_span_id = _gen_span_id()
        if self._active_trace_id:
            tid = self._active_trace_id
            pid = self._active_root_span_id
        else:
            tid = _gen_trace_id()
            pid = ""
        self._buffer_span(name, body, duration_ms, tid, new_span_id, pid)

    def trace_standalone(self, name: str, body: str, duration_ms: int = 500) -> None:
        """Buffer an independent root span (not attached to any active trace)."""
        self._buffer_span(name, body, duration_ms, _gen_trace_id(), _gen_span_id(), "")

    # ------------------------------------------------------------------
    # Flush — metrics
    # ------------------------------------------------------------------

    def push_metrics(
        self,
        stage:       int,
        hunger:      int,
        happiness:   int,
        health:      int,
        age_s:       int,
        care_mistakes: int,
        discipline:  int,
        rssi:        int,
        accel_mag:   float,
        battery_mv:  int = 4100,
    ) -> bool:
        ts_ns = _now_ns()
        ts_str = str(ts_ns)

        def gauge(name: str, value: int) -> dict[str, Any]:
            return {
                "name": name,
                "gauge": {
                    "dataPoints": [{
                        "asInt": value,
                        "timeUnixNano": ts_str,
                    }],
                },
            }

        metrics = [
            gauge("tamagrotchi.stage",         stage),
            gauge("tamagrotchi.hunger",         hunger),
            gauge("tamagrotchi.happiness",      happiness),
            gauge("tamagrotchi.health",         health),
            gauge("tamagrotchi.age_s",          age_s),
            gauge("tamagrotchi.care_mistakes",  care_mistakes),
            gauge("tamagrotchi.discipline",     discipline),
            gauge("tamagrotchi.wifi_rssi",      rssi),
            gauge("tamagrotchi.imu_accel_mag",  int(accel_mag * 100)),
            gauge("tamagrotchi.battery_mv",     battery_mv),
            # Registers this host with Grafana Cloud for Knowledge Graph / App O11y host-hours.
            gauge("target_info",                1),
        ]

        payload = {
            "resourceMetrics": [{
                "resource": {"attributes": self._resource_attrs()},
                "scopeMetrics": [{
                    "scope": {"name": "tamagrotchi"},
                    "metrics": metrics,
                }],
            }],
        }

        return self._post(self._metrics_url, payload, "metrics")

    # ------------------------------------------------------------------
    # Flush — logs
    # ------------------------------------------------------------------

    def flush_logs(self) -> bool:
        with self._lock:
            snap = list(self._log_buf)
            self._log_buf.clear()

        if not snap:
            return True

        records = []
        for e in snap:
            records.append({
                "timeUnixNano":   str(e.ts_ns),
                "severityNumber": e.severity,
                "severityText":   _sev_text(e.severity),
                "body": {"stringValue": e.body},
                "attributes": [{
                    "key": "event",
                    "value": {"stringValue": e.event},
                }],
            })

        payload = {
            "resourceLogs": [{
                "resource": {"attributes": self._resource_attrs({"env": "sciencefair"})},
                "scopeLogs": [{
                    "logRecords": records,
                }],
            }],
        }

        return self._post(self._logs_url, payload, f"logs({len(snap)})")

    # ------------------------------------------------------------------
    # Flush — traces
    # ------------------------------------------------------------------

    def flush_traces(self) -> bool:
        with self._lock:
            snap = list(self._span_buf)
            self._span_buf.clear()

        if not snap:
            return True

        spans = []
        for se in snap:
            dur = se.duration_ms if se.duration_ms > 0 else 500
            end_ns = se.start_ns + dur * 1_000_000

            sp: dict[str, Any] = {
                "traceId":           se.trace_id,
                "spanId":            se.span_id,
                "name":              se.name,
                "kind":              1,   # INTERNAL
                "startTimeUnixNano": str(se.start_ns),
                "endTimeUnixNano":   str(end_ns),
                "status": {"code": 1},    # OK
                "attributes": [{
                    "key": "event.details",
                    "value": {"stringValue": se.body},
                }],
            }
            if se.parent_span_id:
                sp["parentSpanId"] = se.parent_span_id

            spans.append(sp)

        payload = {
            "resourceSpans": [{
                "resource": {"attributes": self._resource_attrs({"deployment.environment": "grafanacon"})},
                "scopeSpans": [{
                    "scope": {"name": "tamagrotchi-firmware"},
                    "spans": spans,
                }],
            }],
        }

        return self._post(self._traces_url, payload, f"traces({len(snap)})")

    # ------------------------------------------------------------------
    # Internal HTTP helper
    # ------------------------------------------------------------------

    def _post(self, url: str, payload: dict[str, Any], label: str) -> bool:
        try:
            resp = self._session.post(url, json=payload, timeout=10)
            ok = 200 <= resp.status_code < 300
            if self._verbose:
                status = "ok" if ok else "FAIL"
                print(f"    [{self._game_id}] {label} {status} {resp.status_code}")
            return ok
        except Exception as exc:
            if self._verbose:
                print(f"    [{self._game_id}] {label} ERROR: {exc}")
            return False
