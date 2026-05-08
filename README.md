# Tamagrotchi

A Tamagotchi for Grafana users, featuring **Grot** as your virtual pet.
Keep Grot alive, watch him evolve, and observe his vital signs flowing into Grafana Cloud as OTLP metrics, logs, and traces.

Built for Grafana Labs Hackathon and featuring at the GrafanaCon Science Fair

<img width="2463" height="1151" alt="image" src="https://github.com/user-attachments/assets/6bf8a0a8-1c5c-4d36-9dbd-52924fc1d1eb" />

---

## Building & Flashing

```bash
# Install PlatformIO CLI if needed
pip install platformio

# Build
pio run

# Flash firmware
pio run --target upload

# Flash filesystem (config.json)
pio run --target uploadfs

# Serial monitor
pio device monitor
```

---

## Configuration

Copy `data/config.json.example` to `data/config.json` and fill in your credentials:

```json
{
  "wifi": {
    "ssid": "YOUR_SSID",
    "password": "YOUR_PASSWORD"
  },
  "grafana": {
    "otlp_base": "https://otlp-gateway-prod-<region>.grafana.net/otlp",
    "auth_b64":  "Basic <your_grafana_cloud_token>",
    "device_id": "tamagrotchi"
  },
  "game": {
    "demo_speed": false,
    "push_interval_s": 60,
    "buzzer": true,
    "flash_alerts": true
  },
  "display": {
    "bgr_order": false
  }
}
```

If your display colours seem odd, then set bgr_order to true

> **Auth note:** `auth_b64` must be `Basic <raw_glc_token>` — Grafana Cloud OTLP accepts the token directly after "Basic", not base64-encoded.

Upload the filesystem image after editing: **PlatformIO → Upload Filesystem Image**.

---


## Hardware

The physical device is built around a Waveshare ESP32-S3-Touch-LCD-1.69

| Component | Detail |
|---|---|
| MCU | ESP32-S3, dual-core 240 MHz, 8 MB PSRAM |
| Display | 240×280 ST7789V2 IPS, SPI @ 40 MHz |
| IMU | QMI8658C (accel + gyro) via I2C |
| RTC | PCF85063A via I2C |
| Touch | CST816 capacitive (I2C) |
| Buttons | A / B / C (GPIO3, 17, 18 — active-LOW) |
| Buzzer | GPIO42 via LEDC |
| Battery | LiPo with ADC divider on GPIO1 |

A printable case is included in `hardware/` (STL + 3MF).

Instructions are available at [hardware/hardware-bom.md](hardware/hardware-bom.md)

---

## The App

### Grot's Life Cycle
- 6 life stages: **Egg → Baby → Child → Teen → Adult → Senior**
- Evolution triggered by age thresholds, with an "EVOLVING…" overlay and melody
- **Evolution quality** (Excellent / Good / Tired) determined by accumulated care mistakes — affects which sprite set is shown

### Vitals
- **Hunger**, **Happiness**, **Health** — all 0–100
- Stats decay each game tick; health slowly recovers when both hunger and happiness are above 50
- Alerts fire (buzzer + log) when either falls below threshold
- Sustained low hunger causes sickness; untreated sickness causes death

### Player Actions
| Button | Action |
|---|---|
| A | Previous Menu |
| B | Select |
| C | Next Menu |
| B (when Idle) | Cycle through animations |
| Gentle shake | Play / wake from sleep |
| Hard shake | Dizzy (-5 happiness, status effect) |

### Sleep / Wake
- Grot sleeps from 22:00 to 08:00 (RTC-based)
- Waking him early is a care mistake
- NTP time is synced to the hardware RTC on first WiFi connection

### Sprites
All frames use `LV_COLOR_FORMAT_RGB565A8` (RGB565 + alpha map), rendered at 3× scale via LVGL image-level transforms.

| Sprite sheet | Size | Frames | Used for |
|---|---|---|---|
| `egg-shake.png` | 38×38 | 3 | Egg idle + periodic shake animation |
| `grot.png` | 32×32 | 4 | Baby idle, tired adult idle |
| `grot-blink.png` | 32×32 | 2 | Sleeping |
| `grot-wave.png` | 32×32 | 6 | Happy (baby, child, good adult) |
| `grot-walk.png` | 32×32 | 8 | Child idle, excellent adult idle (+ left-facing variant) |
| `grot-jumping.png` | 32×32 | 8 | Happy (excited adult), dizzy (last 4 fr) |
| `grot-thinks.png` | 32×32 | 5 | Sad, sick, dead |

The egg sits still for ~3 s then shakes (three 50 ms frames) before returning to rest.

### Telemetry → Grafana Cloud
All telemetry is sent via **OTLP/HTTP JSON** to Grafana Cloud on a configurable interval (default 60 s).

**Metrics** (`/v1/metrics`):
- `grot.hunger`, `grot.happiness`, `grot.health` — vital signs
- `grot.age_seconds` — pet age
- `grot.care_mistakes` — evolution quality indicator
- `grot.battery_voltage` — LiPo charge level
- `grot.wifi_rssi` — signal strength
- `grot.accel_mag` — IMU acceleration magnitude

**Logs** (`/v1/logs`): structured events including boot, hunger alerts, happiness alerts, feeding, playing, sickness, evolution, and death.

**Traces** (`/v1/traces`): spans for user actions (feed, play, medicine, discipline) and game events.

Each device generates a random `game_id` (e.g. `Lokirzu`, `Grotpup`) from ~2 025 combinations, used as `service.name` across all three signals so metrics, logs, and traces correlate automatically in Grafana.

The OTLP push runs on a background FreeRTOS task (Core 0) so the game loop is never blocked by network I/O. A small notification confirms each push.


---

## PC Simulator

`simulator/` is a Python multi-instance simulator that produces **format-identical OTLP telemetry** to the ESP32 firmware — useful for load-testing dashboards or generating a rich dataset without hardware.

```bash
cd simulator
pip install -r requirements.txt

python sim.py \
  --instances 20 \
  --otlp-base https://otlp-gateway-prod-us-east-0.grafana.net/otlp \
  --auth "Basic glc_..." \
  --speed 1.0 \
  --push-interval 30
```

Or create `simulator/config.json` (see `config.json.example`) and just run `python sim.py`.

We also have a helm chart to deploy a fleet of Grots to your k8s cluster

**Options:**

| Flag | Default | Description |
|---|---|---|
| `--instances N` | 5 | Number of simultaneous pets |
| `--otlp-base URL` | — | OTLP gateway base URL |
| `--auth TOKEN` | — | `Basic glc_...` auth header |
| `--speed FLOAT` | 1.0 | Simulation speed multiplier |
| `--push-interval S` | 30 | OTLP push interval in seconds |
| `--verbose` | off | Print per-push HTTP status |
| `--no-traces` | off | Skip trace pushes |
| `--no-logs` | off | Skip log pushes |

Instance 0 is always named **Beylazu** — a fixed anchor for dashboards. All other instances get randomised `game_id` names.

---


---



## Adding / Updating Sprites

```bash
# Re-convert a sprite sheet (frames arranged horizontally, height = frame size)
python3 tools/png_to_lvgl.py src/sprites/grot-wave.png

# The script outputs one .cpp file per frame, e.g. grot_wave_0.cpp … grot_wave_5.cpp
# Wire new frames into src/sprites/grot_frames.cpp
```

The egg sprite (`egg-shake.png`) uses 38×38 px frames; all other sprites use 32×32 px.

---

## Tech Stack

| Layer | Library |
|---|---|
| Framework | Arduino (ESP-IDF 5.x underneath) |
| Build system | PlatformIO |
| Display driver | LovyanGFX 1.1.16 |
| UI toolkit | LVGL v9.3.0 |
| JSON | ArduinoJson 7.1.0 |
| IMU | FastIMU 1.2.6 |
| Filesystem | LittleFS |
| Telemetry | OTLP/HTTP JSON (custom, no SDK) |

---


## Contributing

Using Visual Studio Code, the easiest way is to install the PlatformIO extension. You can then rely on the PlatformIO config in the repo to build, monitor, and flash the hardware.
