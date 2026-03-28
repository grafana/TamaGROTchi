#!/usr/bin/env python3
"""
Kubernetes entrypoint for a single Tamagotchi instance.

Reads configuration from environment variables (injected by Helm):
    OTLP_BASE        — OTLP gateway base URL (required)
    OTLP_AUTH        — Authorization header value, e.g. "Basic glc_..." (required)
    DEVICE_PREFIX    — Device ID prefix (default: sim)
    SPEED            — Simulation speed multiplier (default: 1.0)
    PUSH_INTERVAL    — OTLP push interval in seconds (default: 30)
    VERBOSE          — "true" to enable verbose HTTP logging (default: false)
    NO_TRACES        — "true" to skip trace pushes (default: false)
    NO_LOGS          — "true" to skip log pushes (default: false)

The pod index is read from JOB_COMPLETION_INDEX (set automatically by
Kubernetes Indexed Jobs). The device ID becomes {DEVICE_PREFIX}-{INDEX:02d}.

When the pet dies the process exits with code 0 — the pod enters Completed
state and is not restarted (restartPolicy: Never).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from sim import TamagotchiInstance


def _env(key: str, default: str = "") -> str:
    return os.environ.get(key, default)


def _env_bool(key: str) -> bool:
    return _env(key, "false").lower() in ("1", "true", "yes")


def main() -> None:
    otlp_base = _env("OTLP_BASE")
    otlp_auth = _env("OTLP_AUTH")

    if not otlp_base:
        print("ERROR: OTLP_BASE environment variable is required", file=sys.stderr)
        sys.exit(2)
    if not otlp_auth:
        print("ERROR: OTLP_AUTH environment variable is required", file=sys.stderr)
        sys.exit(2)

    index        = int(_env("JOB_COMPLETION_INDEX", "0"))
    device_prefix = _env("DEVICE_PREFIX", "sim")
    speed        = float(_env("SPEED", "1.0"))
    push_interval = float(_env("PUSH_INTERVAL", "30.0"))
    verbose      = _env_bool("VERBOSE")
    send_traces  = not _env_bool("NO_TRACES")
    send_logs    = not _env_bool("NO_LOGS")

    device_id = f"{device_prefix}-{index:02d}"

    print(f"[tamagotchi] Starting pet: device_id={device_id} index={index}")
    print(f"[tamagotchi] OTLP base:     {otlp_base}")
    print(f"[tamagotchi] Speed:         {speed}x")
    print(f"[tamagotchi] Push interval: {push_interval}s")
    print(f"[tamagotchi] Traces:        {'yes' if send_traces else 'no'}")
    print(f"[tamagotchi] Logs:          {'yes' if send_logs else 'no'}")

    inst = TamagotchiInstance(
        index=index,
        device_id=device_id,
        otlp_base=otlp_base,
        auth=otlp_auth,
        speed=speed,
        push_interval=push_interval,
        verbose=verbose,
        send_traces=send_traces,
        send_logs=send_logs,
    )

    inst.run()  # Returns when pet dies (status == DEAD)

    print(f"[tamagotchi] {device_id} ({inst.game_id}) has died. Pod exiting.")
    sys.exit(0)


if __name__ == "__main__":
    main()
