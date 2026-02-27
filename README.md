# Tamagrotchi

A Tamagotchi for Grafana employees, featuring **Grot** — the Grafana mascot — as your virtual pet. Keep Grot alive, watch him evolve, and observe his vital signs flowing into Grafana Cloud as OTLP metrics and logs.

Built for [Grafana Labs Hackathon #16](https://github.com/grafana/hackathon-16-tamagrotchi).

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

---

## What's Working

### Grot's Life Cycle
- 6 life stages: **Egg → Baby → Child → Teen → Adult → Senior**
- Evolution triggered by age thresholds, with a 3-second "EVOLVING…" overlay and melody
- **Evolution quality** (Excellent / Good / Tired) determined by accumulated care mistakes — affects which sprite set is shown

### Vitals
- **Hunger**, **Happiness**, **Health** — all 0–100
- Stats decay each game tick; health slowly recovers when both hunger and happiness are above 50
- Alerts fire (buzzer + log) when either falls below threshold
- Sustained low hunger causes sickness; untreated sickness causes death

### Player Actions
| Button | Action |
|---|---|
| A | Feed (+25 hunger) |
| C | Play (+20 happiness) |
| B | Discipline |
| B (hold) | Give medicine (clears sick status) |
| Gentle shake | Play / wake from sleep |
| Hard shake | Dizzy (-5 happiness, status effect) |

### Sleep / Wake
- Grot sleeps from 22:00 to 08:00 (RTC-based)
- Waking him early is a care mistake
- NTP time is synced to the hardware RTC on first WiFi connection

### Sprites
- 33 animation frames across 6 sprite sheets, all 32×32 px RGBA
- Rendered at 170×170 px (5.3× scale) to fill the sprite zone
- Format: `LV_COLOR_FORMAT_RGB565A8` (RGB565 + alpha map)
- Frames are mapped to life stage + emotion + quality:

| Animation | Used for |
|---|---|
| `grot.png` (4 fr) | Egg idle, baby idle, tired adult idle |
| `grot-blink.png` (2 fr) | Sleeping |
| `grot-wave.png` (6 fr) | Happy (baby, child, good adult) |
| `grot-walk.png` (8 fr) | Child idle, excellent adult idle |
| `grot-jumping.png` (8 fr) | Happy (excited adult), dizzy (last 4 fr) |
| `grot-thinks.png` (5 fr) | Sad, sick, dead |

### Telemetry → Grafana Cloud
All metrics and logs are sent via **OTLP/HTTP JSON** to Grafana Cloud on a configurable interval (default 60 s).

**Metrics** (`/v1/metrics`):
- `grot.hunger`, `grot.happiness`, `grot.health` — vital signs
- `grot.age_seconds` — pet age
- `grot.care_mistakes` — evolution quality indicator
- `grot.battery_voltage` — LiPo charge level
- `grot.wifi_rssi` — signal strength
- `grot.accel_mag` — IMU acceleration magnitude

**Logs** (`/v1/logs`): structured events including boot, hunger alerts, happiness alerts, feeding, playing, sickness, evolution, and death.

The OTLP push runs on a background FreeRTOS task (Core 0) so the game loop is never blocked by network I/O. A small toast notification confirms each push.

### UI Layout (240×280)
```
┌─────────────────────────┐  y=0   h=30   Header: age + WiFi status
│  Age: 0m        WiFi ✓  │
├─────────────────────────┤  y=30  h=170  Sprite zone (Grot, 170×170)
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
│   ├── sprite_engine.cpp     # LVGL image object + animation timer
│   ├── grot*.png             # Source sprite sheets (32 px tall, RGBA)
│   └── grot_*.cpp            # Auto-generated LVGL C arrays (33 files)
├── telemetry/
│   └── otlp_writer.cpp       # OTLP/HTTP JSON push, background FreeRTOS task
├── input/buttons.cpp         # Debounced button event ring buffer
├── buzzer/buzzer.cpp         # LEDC tone player, async melody queue
├── imu/imu_driver.cpp        # QMI8658C accel/gyro, shake detection
├── rtc/rtc_driver.cpp        # PCF85063A read/write, NTP sync
├── power/battery.cpp         # ADC battery voltage + percentage
└── wifi/wifi_manager.cpp     # Non-blocking WiFi FSM + SNTP
tools/
└── png_to_lvgl.py            # Converts sprite sheet PNGs → LVGL v9 C++ arrays
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
    "push_interval_s": 60,
    "buzzer": true,
    "flash_alerts": true
  }
}
```

`auth_b64` must be `Basic <raw_glc_token>` — Grafana Cloud OTLP accepts the raw token directly (do **not** base64-encode it again).

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
# Re-convert a sprite sheet (32 px tall, any multiple of 32 px wide)
python3 tools/png_to_lvgl.py src/sprites/grot-wave.png

# The script outputs one .cpp file per frame, e.g. grot_wave_0.cpp … grot_wave_5.cpp
# Wire new frames into src/sprites/grot_frames.cpp
```

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

- [ ] Touch screen interactions (currently unused)
- [ ] Save/restore pet state to NVS across reboots
- [ ] Mini-games for the play action
- [ ] More sprite states (eating animation, sick animation)
- [ ] Grafana dashboard template for the metrics
- [ ] OTA firmware updates
