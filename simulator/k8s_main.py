#!/usr/bin/env python3
"""
Kubernetes entrypoint for a single TamaGROTchi instance.

Reads configuration from environment variables (injected by Helm):
    OTLP_BASE        — OTLP gateway base URL (required)
    OTLP_AUTH        — Authorization header value, e.g. "Basic glc_..." (required)
    MY_POD_NAME      — Pod name from Downward API; used as device_id (required in k8s)
    SPEED            — Simulation speed multiplier (default: 1.0)
    PUSH_INTERVAL    — OTLP push interval in seconds (default: 30)
    VERBOSE          — "true" to enable verbose HTTP logging (default: false)
    NO_TRACES        — "true" to skip trace pushes (default: false)
    NO_LOGS          — "true" to skip log pushes (default: false)

When the pet dies the process exits with code 0. The Deployment restartPolicy
(Always) causes Kubernetes to restart the container as a fresh pet with the
same pod name.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from sim import TamagrotchiInstance


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

    device_id    = _env("MY_POD_NAME", "sim-local")
    speed        = float(_env("SPEED", "1.0"))
    push_interval = float(_env("PUSH_INTERVAL", "30.0"))
    verbose      = _env_bool("VERBOSE")
    send_traces  = not _env_bool("NO_TRACES")
    send_logs    = not _env_bool("NO_LOGS")

    # Kubernetes resource attributes for Grafana Cloud Knowledge Graph.
    # Only populated when running inside a pod (env vars injected by Helm Downward API).
    k8s_attrs: dict[str, str] = {}
    for env_key, attr_key in [
        ("MY_POD_NAME",       "k8s.pod.name"),
        ("MY_POD_NAMESPACE",  "k8s.namespace.name"),
        ("MY_NODE_NAME",      "k8s.node.name"),
        ("MY_DEPLOYMENT_NAME","k8s.deployment.name"),
    ]:
        val = _env(env_key)
        if val:
            k8s_attrs[attr_key] = val

    print(f"[tamagrotchi] Starting pet: device_id={device_id}")
    print(f"[tamagrotchi] OTLP base:     {otlp_base}")
    print(f"[tamagrotchi] Speed:         {speed}x")
    print(f"[tamagrotchi] Push interval: {push_interval}s")
    print(f"[tamagrotchi] Traces:        {'yes' if send_traces else 'no'}")
    print(f"[tamagrotchi] Logs:          {'yes' if send_logs else 'no'}")

    while True:
        inst = TamagrotchiInstance(
            device_id=device_id,
            otlp_base=otlp_base,
            auth=otlp_auth,
            speed=speed,
            push_interval=push_interval,
            verbose=verbose,
            send_traces=send_traces,
            send_logs=send_logs,
            k8s_attrs=k8s_attrs,
        )

        inst.run()  # Returns when pet dies (status == DEAD)

        print(f"[tamagrotchi] {device_id} ({inst.game_id}) has died. Respawning...")
        time.sleep(2)


if __name__ == "__main__":
    main()
