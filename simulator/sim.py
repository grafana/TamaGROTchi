#!/usr/bin/env python3
"""
Tamagotchi PC Simulator
=======================
Runs N simultaneous Tamagotchi instances, each with its own game loop,
random action scheduler, and OTLP telemetry push — output is format-identical
to the ESP32-S3 firmware.

Usage:
    python sim.py --instances 20 \\
        --otlp-base https://otlp-gateway-prod-us-east-0.grafana.net/otlp \\
        --auth "Basic glc_..." \\
        --device-prefix sim \\
        --speed 1.0 \\
        --push-interval 30

Options:
    --instances N        Number of simultaneous pets (default: 5)
    --otlp-base URL      OTLP gateway base URL
    --auth TOKEN         Authorization header value (e.g. "Basic glc_...")
    --device-prefix PFX  Device ID prefix; IDs become PFX-00 … PFX-NN (default: sim)
    --speed FLOAT        Simulation speed multiplier; 2.0 = 2× faster (default: 1.0)
    --push-interval S    OTLP push interval in seconds (default: 30)
    --verbose            Print per-push HTTP status lines
    --no-traces          Skip trace/span pushes (saves quota)
    --no-logs            Skip log pushes (saves quota)

Config file:
    Instead of CLI flags, create simulator/config.json:
    {
        "otlp_base":    "https://...",
        "auth":         "Basic glc_...",
        "device_prefix":"sim",
        "instances":    20,
        "speed":        1.0,
        "push_interval":30
    }
    CLI flags override config.json values.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import sys
import threading
import time
from pathlib import Path
from typing import Optional

# Ensure the simulator/ directory is on the path
sys.path.insert(0, str(Path(__file__).parent))

from game_logic import (
    action_discipline,
    action_feed,
    action_medicine,
    action_play,
    action_shake,
    evolution_check,
    tick,
)
from otlp_client import OtlpClient
from pet_state import LifeStage, PetState, PetStatus, life_stage_name

# ---------------------------------------------------------------------------
# Game-ID word lists (mirrors firmware generate_game_id)
# ---------------------------------------------------------------------------
_PREFIXES = [
    "Loki", "Mimir", "Tempo", "Torkel", "Otel", "Prom",
    "Pyro", "Alloy", "Grot", "Beyla", "Faro", "Tanka",
    "Cortex", "Thanos", "Agent", "Oncall", "Sift", "Graph", "Panel", "Flux"
]
_SUFFIXES = [
    "tchi", "kins", "ito",  "bot",  "zu",  "mon", "ie",  "let", "y",
    "pup",  "pet",  "ling", "ster", "kun", "chan", "tan", "nu",  "mu",
    "chi",  "ko",   "ka",   "boo",  "moo", "doo", "pop",
    "pip",  "tip",  "nip",  "bug",  "mug", "pug",
    "wix",  "nix",  "mix",  "pix",  "zee", "bee",
    "coo",  "goo",  "loo",  "sox",  "fox", "box",
    "pod",  "nod",
]


def _generate_game_id() -> str:
    return random.choice(_PREFIXES) + random.choice(_SUFFIXES)


# ---------------------------------------------------------------------------
# TamagotchiInstance — one pet running in its own thread
# ---------------------------------------------------------------------------

class TamagotchiInstance:
    """
    Wraps PetState + OtlpClient + game tick loop + random action scheduler.

    Provides the 'sim' interface expected by game_logic.py:
        sim.log(severity, event, body)
        sim.begin_trace(name, body, duration_ms)
        sim.end_trace()
        sim.active_trace_child(name, body, duration_ms)
        sim.trace_standalone(name, body, duration_ms)
    """

    def __init__(
        self,
        index:         int,
        device_id:     str,
        otlp_base:     str,
        auth:          str,
        speed:         float,
        push_interval: float,
        verbose:       bool,
        send_traces:   bool,
        send_logs:     bool,
    ) -> None:
        self.index        = index
        self.device_id    = device_id
        self.game_id      = _generate_game_id()
        self.speed        = speed
        self.push_interval = push_interval
        self.send_traces  = send_traces
        self.send_logs    = send_logs

        self.pet   = PetState()
        self.otlp  = OtlpClient(
            otlp_base=otlp_base,
            auth_b64=auth,
            device_id=device_id,
            game_id=self.game_id,
            verbose=verbose,
        )

        self._stop_event    = threading.Event()
        self._last_push     = 0.0
        self._last_event    = "boot"
        self._lock          = threading.Lock()

        # Scheduled action times (monotonic seconds)
        self._next_feed      = 0.0
        self._next_play      = 0.0
        self._next_medicine  = 0.0
        self._next_discipline = 0.0
        self._schedule_all()

        # Simulated RSSI: wander between -55 and -80
        self._rssi = random.randint(-75, -55)

    # ------------------------------------------------------------------
    # 'sim' interface used by game_logic.py
    # ------------------------------------------------------------------

    def log(self, severity: int, event: str, body: str) -> None:
        self._last_event = event
        if self.send_logs:
            self.otlp.log(severity, event, body)

    def begin_trace(self, name: str, body: str, duration_ms: int = 0) -> None:
        if self.send_traces:
            self.otlp.trace_begin(name, body, duration_ms)

    def end_trace(self) -> None:
        if self.send_traces:
            self.otlp.trace_end()

    def active_trace_child(self, name: str, body: str, duration_ms: int = 500) -> None:
        if self.send_traces:
            self.otlp.trace(name, body, duration_ms)

    def trace_standalone(self, name: str, body: str, duration_ms: int = 500) -> None:
        if self.send_traces:
            self.otlp.trace_standalone(name, body, duration_ms)

    # ------------------------------------------------------------------
    # Scheduling helpers
    # ------------------------------------------------------------------

    def _schedule_all(self) -> None:
        now = time.monotonic()
        self._next_feed       = now + self._jitter(90)
        self._next_play       = now + self._jitter(120)
        self._next_medicine   = now + self._jitter(300)
        self._next_discipline = now + self._jitter(180)

    def _jitter(self, mean_s: float) -> float:
        """Draw from an exponential distribution scaled by speed."""
        return random.expovariate(1.0 / mean_s) / self.speed

    # ------------------------------------------------------------------
    # Main loop (runs in its own thread)
    # ------------------------------------------------------------------

    def run(self) -> None:
        tick_interval = 1.0 / self.speed
        self.log(9, "boot", f"stage=egg | simulator=true | game_id={self.game_id}")

        while not self._stop_event.is_set():
            loop_start = time.monotonic()

            # --- Game tick ---
            tick(self.pet, self)

            if self.pet.status == PetStatus.DEAD:
                # Wait a short time then quietly exit
                time.sleep(5.0 / self.speed)
                break

            # --- Random action scheduler ---
            self._maybe_do_actions()

            # --- OTLP flush ---
            now = time.monotonic()
            if now - self._last_push >= (self.push_interval / self.speed):
                self._do_push()
                self._last_push = now

            # --- Sleep for remainder of tick ---
            elapsed = time.monotonic() - loop_start
            sleep_s = max(0.0, tick_interval - elapsed)
            self._stop_event.wait(timeout=sleep_s)

    def _maybe_do_actions(self) -> None:
        now = time.monotonic()
        p   = self.pet

        # Feed
        if now >= self._next_feed:
            food = random.choice(["microchip", "sin_wave"])
            self.begin_trace("user.feed", f"food={food}", 600)
            action_feed(p, food, self)
            self.end_trace()
            self._next_feed = now + self._jitter(90)

        # Play
        if now >= self._next_play:
            self.begin_trace("user.play", "action=play", 600)
            action_play(p, self)
            self.end_trace()
            self._next_play = now + self._jitter(120)

        # Medicine (only when health is low)
        if now >= self._next_medicine and p.health < 70:
            self.begin_trace("user.medicine", "action=medicine", 600)
            action_medicine(p, self)
            self.end_trace()
            self._next_medicine = now + self._jitter(300)
        elif now >= self._next_medicine:
            # Reschedule without acting
            self._next_medicine = now + self._jitter(300)

        # Discipline
        if now >= self._next_discipline:
            self.begin_trace("user.discipline", "action=discipline", 600)
            action_discipline(p, self)
            self.end_trace()
            self._next_discipline = now + self._jitter(180)

        # Random shake (5% chance per tick)
        if random.random() < 0.05:
            mag = random.uniform(1.0, 4.5)
            action_shake(p, mag, self)

        # Drift RSSI slightly
        self._rssi = max(-85, min(-45, self._rssi + random.randint(-2, 2)))

    def _do_push(self) -> None:
        p = self.pet
        self.otlp.push_metrics(
            stage=int(p.stage),
            hunger=p.hunger,
            happiness=p.happiness,
            health=p.health,
            age_s=p.age_s,
            care_mistakes=p.care_mistakes,
            discipline=p.discipline,
            rssi=self._rssi,
            accel_mag=p.last_accel_mag,
        )
        if self.send_logs:
            self.otlp.flush_logs()
        if self.send_traces:
            self.otlp.flush_traces()

    def stop(self) -> None:
        self._stop_event.set()

    def summary(self) -> dict:
        p = self.pet
        return {
            "idx":        self.index,
            "game_id":    self.game_id,
            "device":     self.device_id,
            "stage":      life_stage_name(p.stage),
            "hunger":     p.hunger,
            "happiness":  p.happiness,
            "health":     p.health,
            "age_s":      p.age_s,
            "mistakes":   p.care_mistakes,
            "status":     p.status.name,
            "last_event": self._last_event,
        }


# ---------------------------------------------------------------------------
# Status table printer
# ---------------------------------------------------------------------------

def _print_table(instances: list[TamagotchiInstance]) -> None:
    header = (
        f"{'#':>3}  {'game_id':<14}  {'device':<10}  "
        f"{'stage':<8}  {'hun':>4}  {'hap':>4}  {'hp':>4}  "
        f"{'age_s':>6}  {'err':>4}  {'status':<10}  {'last_event'}"
    )
    print("\n" + "─" * len(header))
    print(header)
    print("─" * len(header))
    for inst in instances:
        s = inst.summary()
        print(
            f"{s['idx']:>3}  {s['game_id']:<14}  {s['device']:<10}  "
            f"{s['stage']:<8}  {s['hunger']:>4}  {s['happiness']:>4}  {s['health']:>4}  "
            f"{s['age_s']:>6}  {s['mistakes']:>4}  {s['status']:<10}  {s['last_event']}"
        )
    print("─" * len(header))


# ---------------------------------------------------------------------------
# Config loading
# ---------------------------------------------------------------------------

def _load_config_file() -> dict:
    config_path = Path(__file__).parent / "config.json"
    if config_path.exists():
        with open(config_path) as f:
            return json.load(f)
    return {}


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    file_cfg = _load_config_file()

    parser = argparse.ArgumentParser(
        description="Tamagotchi PC Simulator — multi-instance OTLP telemetry generator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--instances",      type=int,   default=file_cfg.get("instances",     5))
    parser.add_argument("--otlp-base",      type=str,   default=file_cfg.get("otlp_base",     ""))
    parser.add_argument("--auth",           type=str,   default=file_cfg.get("auth",          ""))
    parser.add_argument("--device-prefix",  type=str,   default=file_cfg.get("device_prefix", "sim"))
    parser.add_argument("--speed",          type=float, default=file_cfg.get("speed",         1.0))
    parser.add_argument("--push-interval",  type=float, default=file_cfg.get("push_interval", 30.0))
    parser.add_argument("--verbose",        action="store_true", default=file_cfg.get("verbose", False))
    parser.add_argument("--no-traces",      action="store_true")
    parser.add_argument("--no-logs",        action="store_true")
    parser.add_argument("--status-interval",type=float, default=30.0,
                        help="Seconds between status table prints (default: 30)")

    args = parser.parse_args()

    if not args.otlp_base:
        print("ERROR: --otlp-base is required (or set 'otlp_base' in simulator/config.json)")
        sys.exit(1)
    if not args.auth:
        print("ERROR: --auth is required (or set 'auth' in simulator/config.json)")
        sys.exit(1)

    send_traces = not args.no_traces
    send_logs   = not args.no_logs

    print(f"Starting {args.instances} Tamagotchi instance(s)")
    print(f"  OTLP base:     {args.otlp_base}")
    print(f"  Speed:         {args.speed}×")
    print(f"  Push interval: {args.push_interval}s")
    print(f"  Traces:        {'yes' if send_traces else 'no'}")
    print(f"  Logs:          {'yes' if send_logs else 'no'}")
    print()

    instances: list[TamagotchiInstance] = []
    threads:   list[threading.Thread]   = []

    for i in range(args.instances):
        device_id = f"{args.device_prefix}-{i:02d}"
        inst = TamagotchiInstance(
            index=i,
            device_id=device_id,
            otlp_base=args.otlp_base,
            auth=args.auth,
            speed=args.speed,
            push_interval=args.push_interval,
            verbose=args.verbose,
            send_traces=send_traces,
            send_logs=send_logs,
        )
        # Instance 0 is always named Beylazu
        if i == 0:
            inst.game_id = "Beylazu"
            inst.otlp._game_id = "Beylazu"
        print(f"  [{i:02d}] {device_id} → game_id={inst.game_id}")
        instances.append(inst)

        t = threading.Thread(target=inst.run, name=f"tama-{i:02d}", daemon=True)
        t.start()
        threads.append(t)

    print(f"\nAll {args.instances} instance(s) running. Ctrl-C to stop.\n")

    try:
        while True:
            time.sleep(args.status_interval)
            _print_table(instances)
    except KeyboardInterrupt:
        print("\nShutting down…")
        for inst in instances:
            inst.stop()
        # Give threads a moment to flush
        for t in threads:
            t.join(timeout=5.0)
        print("Done.")


if __name__ == "__main__":
    main()
