# Tamagrotchi

A Tamagotchi for Grafana employees, featuring **Grot** as your virtual pet.
Keep Grot alive, watch him evolve, and observe his vital signs flowing into Grafana Cloud as OTLP metrics, logs, and traces.

Built for [Grafana Labs Hackathon #16](https://devpost.team/grafana-bl/projects/14135).

---

## Hardware

**Waveshare ESP32-S3-Touch-LCD-1.69**

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

### UI Layout (240×280)
```
┌─────────────────────────┐  y=0   h=30   Header: age + WiFi status
│  Age: 0m        WiFi ✓  │
├─────────────────────────┤  y=30  h=170  Sprite zone (Grot)
│                         │
│          GROT           │
│                         │
├─────────────────────────┤  y=200 h=22   Vitals bars (hunger/happy/health)
│ ████░░   ███░░   ████░  │
├─────────────────────────┤  y=222 h=20   Bar legend
│ Hunger  Happiness  Health│
├─────────────────────────┤  y=242 h=38   Button hints
│ [A] Feed  [B] Menu  [C] Play │
└─────────────────────────┘
```

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

## Project Structure

```
src/
├── main.cpp                  # Arduino setup/loop, wires everything together
├── config.h                  # Pin definitions, game tuning constants
├── config_mgr/               # LittleFS JSON config loader
├── display/
│   ├── lgfx_config.h         # LovyanGFX ST7789V2 panel config
│   ├── lvgl_port.cpp         # LVGL init, flush callback, draw buffers in PSRAM
│   ├── lvgl_mem.cpp          # LVGL custom allocator → PSRAM (frees ~147 KB DRAM)
│   └── ui_screens.cpp        # LVGL screen layout and update API
├── game/
│   ├── pet_state.h/cpp       # PetState struct + init
│   ├── game_engine.cpp       # 1-second tick, stat decay, alerts, sleep/wake
│   ├── evolution.cpp         # Stage thresholds and quality calculation
│   └── actions.cpp           # Feed, play, medicine, discipline, dizzy, wake
├── sprites/
│   ├── grot_frames.h/cpp     # Frame tables mapping state → sprite arrays
│   ├── sprite_engine.cpp     # LVGL image object + animation/movement timers
│   ├── egg-shake.png         # 3-frame 38×38 egg shake sprite sheet
│   ├── egg_shake_*.cpp       # Auto-generated egg shake frames (0–2)
│   ├── grot*.png             # Source sprite sheets (32×32 px, RGBA)
│   └── grot_*.cpp            # Auto-generated LVGL C arrays
├── telemetry/
│   └── otlp_writer.cpp       # OTLP/HTTP JSON push (metrics + logs + traces)
├── input/buttons.cpp         # Debounced button event ring buffer
├── buzzer/buzzer.cpp         # LEDC tone player, async melody queue
├── imu/imu_driver.cpp        # QMI8658C accel/gyro, shake detection
├── rtc/rtc_driver.cpp        # PCF85063A read/write, NTP sync
├── power/battery.cpp         # ADC battery voltage + percentage
└── wifi/wifi_manager.cpp     # Non-blocking WiFi FSM + SNTP
simulator/
├── sim.py                    # Multi-instance simulator entry point
├── game_logic.py             # Pet tick/action logic (mirrors firmware behaviour)
├── pet_state.py              # PetState + enums
├── otlp_client.py            # OTLP HTTP client (metrics, logs, traces)
├── requirements.txt          # Python dependencies
└── config.json.example       # Simulator config template
tools/
└── png_to_lvgl.py            # Converts sprite sheet PNGs → LVGL v9 C++ arrays
hardware/
├── tamagrotchi-face.stl      # Printable front shell
├── tamagrotchi-back.stl      # Printable back shell
└── tamagrotchi.3mf           # Combined 3MF project file
data/
└── config.json.example       # Copy to config.json and fill in credentials
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
    "push_interval_s": 10,
    "buzzer": true,
    "flash_alerts": true
  }
}
```

> **Auth note:** `auth_b64` must be `Basic <raw_glc_token>` — Grafana Cloud OTLP accepts the token directly after "Basic", not base64-encoded.

Upload the filesystem image after editing: **PlatformIO → Upload Filesystem Image**.

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

## What's Left / Ideas

- [ ] Mini-games for the play action
- [ ] More sprite states (eating animation, sick animation)
- [ ] OTA firmware and config updates

---

## Contributing

Using Visual Studio Code, the easiest way is to install the PlatformIO extension. You can then rely on the PlatformIO config in the repo to build, monitor, and flash the hardware.
