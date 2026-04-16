# Tamagrotchi Simulator

A Python simulator that runs multiple Tamagotchi instances in parallel, using **game logic identical** to the ESP32-S3 firmware. Each virtual pet has its own game loop, random action scheduler, and OTLP telemetry push — output is format-identical to the hardware device for Grafana Cloud.

Use it to load-test dashboards, exercise OTLP pipelines, or develop against the same metrics/logs/traces without physical hardware.

---

## Requirements

- **Python 3.10+** (uses type hints and modern syntax)
- **requests** — see `requirements.txt`

```bash
pip install -r requirements.txt
```

---

## Quick start

1. **Copy the example config** and add your Grafana Cloud OTLP credentials:

   ```bash
   cp config.json.example config.json
   ```

   Edit `config.json`:

   - `otlp_base` — OTLP gateway URL (e.g. `https://otlp-gateway-prod-us-east-0.grafana.net/otlp`)
   - `auth` — Authorization header value (e.g. `Basic glc_eyJ...`). See [Getting the auth value](#getting-the-auth-value) below if you don’t have one yet.

2. **Run the simulator:**

   ```bash
   python sim.py
   ```

   This uses values from `config.json`. You can override any of them with CLI flags (see below).

### Getting the auth value

If you don’t have a Grafana Cloud API token yet:

1. **Sign in** to the [Grafana Cloud Portal](https://grafana.com/auth/sign-in/).
2. Open your **organization**, then select the **stack** you want to send data to (or create one).
3. Go to **Connections** (or **Add new connection**), then find the **OpenTelemetry** or **OTLP** tile and click **Configure** (or **Launch** then Configure).
4. On the OTLP configuration page you’ll see:
   - Your **OTLP endpoint URL** — use this for `otlp_base` (e.g. `https://otlp-gateway-prod-us-east-0.grafana.net/otlp`).
   - Instructions to **create an API token** and the **Authorization** header value.
5. **Create a token** if prompted (e.g. “Create token” or “Generate token”). Grafana Cloud often shows a value like `Basic glc_eyJ...` (a long base64-like string). That is the full header value.
6. **Copy the entire string** (including the word `Basic` and the space) into `config.json` as the `auth` value. No extra encoding is needed — paste it as shown.

If the UI gives you only a **raw token** (e.g. `glc_eyJ...` without `Basic `), set `auth` to exactly:

```text
Basic <paste-the-token-here>
```

So the value is the word `Basic`, a single space, then the token. Do not base64-encode the token yourself; Grafana Cloud tokens are already in the format they expect.

For more detail, see [Send data to the Grafana Cloud OTLP endpoint](https://grafana.com/docs/grafana-cloud/send-data/otlp/send-data-otlp/).

---

## Configuration

| Option | Config key | Default | Description |
|--------|------------|---------|-------------|
| `--instances` | `instances` | 5 | Number of simultaneous virtual pets |
| `--otlp-base` | `otlp_base` | (required) | OTLP gateway base URL |
| `--auth` | `auth` | (required) | Auth header (e.g. `Basic glc_...`) |
| `--device-prefix` | `device_prefix` | `sim` | Device IDs become `sim-00`, `sim-01`, … |
| `--speed` | `speed` | 1.0 | Simulation speed (2.0 = 2× faster) |
| `--push-interval` | `push_interval` | 30 | Seconds between OTLP metric/log/trace pushes |
| `--verbose` | `verbose` | false | Print per-push HTTP status |
| `--no-traces` | — | — | Skip trace/span pushes (saves quota) |
| `--no-logs` | — | — | Skip log pushes (saves quota) |
| `--status-interval` | — | 30 | Seconds between status table prints |

**Config file:** `simulator/config.json`. CLI flags override config values.

**Example with CLI only:**

```bash
python sim.py --instances 20 \
  --otlp-base https://otlp-gateway-prod-us-east-0.grafana.net/otlp \
  --auth "Basic glc_eyJ..." \
  --device-prefix sim \
  --speed 1.0 \
  --push-interval 30
```

---

## What it does

- **One thread per pet.** Each instance has its own `PetState`, game tick loop, and OTLP client.
- **Game tick every 1 second** (scaled by `--speed`): stat decay, alerts, sickness, evolution, death — same constants and logic as `game_engine.cpp` / `evolution.cpp` / `actions.cpp`.
- **Random actions:** feed, play, medicine, discipline, and occasional shake are scheduled with exponential jitter so pets get varied care (and sometimes care mistakes).
- **OTLP push** on the given interval: metrics (`grot.hunger`, `grot.happiness`, etc.), buffered logs, and traces in the same format as the firmware.
- **Status table** printed to stdout every `--status-interval` seconds: device, stage, vitals, age, care mistakes, last event.

When a pet dies (health depleted or old age), its thread exits after a short delay; the others keep running. Use **Ctrl-C** to stop all instances gracefully (they flush before exit).

---

## Layout

| File | Role |
|------|------|
| `sim.py` | Entry point: CLI/config, spawns one thread per instance, status table loop |
| `game_logic.py` | Game tick, evolution, and actions — mirrors firmware `config.h` + game engine |
| `pet_state.py` | `PetState` dataclass and enums (`LifeStage`, `PetStatus`, `EvoQuality`) — mirrors `pet_state.h` |
| `otlp_client.py` | OTLP/HTTP JSON client: metrics, logs, traces — format-identical to `otlp_writer.cpp` |
| `config.json` | Local config (not committed; copy from `config.json.example`) |
| `config.json.example` | Example config with placeholder auth |
| `requirements.txt` | Python deps (`requests`) |

---

## Game behavior (same as firmware)

- **Life stages:** Egg → Baby → Child → Teen → Adult → Senior → Dead (age thresholds in `game_logic.py`).
- **Evolution quality:** Excellent / Good / Tired from care mistakes at evolution time.
- **Vitals:** Hunger, happiness, health 0–100; decay per tick; alerts when hunger or happiness is low; sustained low hunger → sickness → health drain → death.
- **Actions:** Feed (microchip / sin_wave), play, medicine, discipline, shake (gentle → play, hard → dizzy).
- **Sleep:** Simulator does not enforce RTC sleep; `is_sleeping` can be used for future extensions.

Numeric constants (decay rates, thresholds, evolution ages, etc.) are taken from the firmware so dashboards and alerts behave the same as on device.

---

## Telemetry

- **Metrics** (e.g. `grot.hunger`, `grot.happiness`, `grot.health`, `grot.age_seconds`, `grot.care_mistakes`, `grot.battery_voltage` sim, `grot.wifi_rssi`, `grot.accel_mag`) — same names and semantics as firmware; battery is simulated or fixed for sim.
- **Logs** — event + body (e.g. `boot`, `fed`, `evolved`, `sick`, `death`, `alert_missed`).
- **Traces** — spans for user actions and game events (e.g. `user.feed`, `pet.fed`, `pet.evolved`, `pet.died`).

Use `--no-traces` or `--no-logs` to reduce quota usage while still testing metrics.
