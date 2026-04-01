#pragma once

// =============================================================================
// Tamagrotchi — Central Hardware & Tuning Constants
// =============================================================================

// --- Display SPI (Waveshare ESP32-S3-Touch-LCD-1.69) -------------------------
#define PIN_LCD_SCLK        6
#define PIN_LCD_MOSI        7
#define PIN_LCD_CS          5
#define PIN_LCD_DC          4
#define PIN_LCD_RST         8
#define PIN_LCD_BL          15
#define LCD_WIDTH           240
#define LCD_HEIGHT          280
#define LCD_SPI_FREQ        80000000UL  // 80 MHz write
#define LCD_SPI_FREQ_READ   16000000UL

// --- I2C (shared: CST816D touch, QMI8658C IMU, PCF85063 RTC) ----------------
#define PIN_I2C_SDA         11
#define PIN_I2C_SCL         10
#define I2C_FREQ            400000

#define TOUCH_I2C_ADDR      0x15  // CST816D (not used for input)
#define IMU_I2C_ADDR        0x6B  // QMI8658C
#define RTC_I2C_ADDR        0x51  // PCF85063A

// --- Buttons (GPIOs broken out on board header) ------------------------------
#define PIN_BTN_A           3   // Left  / Feed      — header pin GPIO3
#define PIN_BTN_B           17  // Select / Menu     — header pin GPIO17
#define PIN_BTN_C           18  // Right / Play      — header pin GPIO18
#define BTN_DEBOUNCE_MS     30
#define BTN_LONG_PRESS_MS   800

// --- Buzzer ------------------------------------------------------------------
#define PIN_BUZZER          42
#define BUZZER_LEDC_CH      0   // LEDC channel 0 (channel 1 reserved for backlight)
#define BUZZER_LEDC_RES     10  // 10-bit resolution
// Note: passive piezo volume is hardware-only (series resistor on GPIO42).
// Software duty != 512 creates harsh harmonics. Keep at 512 for clean tone.
#define BUZZER_DUTY         512

// --- Battery ADC -------------------------------------------------------------
#define PIN_BAT_ADC         1   // GPIO1 — battery voltage via 200kΩ/100kΩ divider
#define BAT_DIVIDER_RATIO   3.0f  // (R1+R2)/R2 = (200k+100k)/100k
#define BAT_V_FULL          4.2f  // LiPo fully charged
#define BAT_V_EMPTY         3.0f  // LiPo cutoff voltage
#define BAT_ADC_SAMPLES     16    // Readings averaged per measurement

// --- Power -------------------------------------------------------------------
#define PIN_SYS_EN          41  // Hold HIGH to keep power on
#define PIN_SYS_OUT         40  // Power output control

// --- LVGL draw buffer --------------------------------------------------------
#define LV_BUF_LINES        40  // Lines per draw buffer (240×40×2 = 19.2KB each)

// --- Backlight brightness (0-255) -------------------------------------------
// Reducing from 255 saves meaningful current on the LED driver.
// Normal: 150 (~60% duty) — still bright indoors.  Sleep: 30 — just visible.
#define LCD_BL_NORMAL       150
#define LCD_BL_SLEEP        30

// --- Main-loop idle delay ---------------------------------------------------
// A brief yield at the end of each loop() tick lets the RTOS put the CPU
// into its automatic idle sleep between iterations.  10 ms still gives LVGL
// 100 calls/s — well above the 50 ms move-timer cadence.
#define LOOP_IDLE_MS        10

// --- Game tick & telemetry ---------------------------------------------------
#define GAME_TICK_MS               1000UL   // 1 second per game tick
#define TELEMETRY_PUSH_INTERVAL_MS 60000UL  // Push every 60 s (overridden by config.json)
#define TELEMETRY_SAMPLE_INTERVAL_MS 10000UL // Sample metrics every 10 s (overridden by sample_interval_s)
#define METRIC_BATCH_CAP           12       // 12 × ~116 bytes ≈ 1.4 KB RAM
#define WIFI_WAKE_TIMEOUT_MS       15000UL  // Give up waking if no connect in 15 s

// --- Shake detection thresholds (g-force) ------------------------------------
#define SHAKE_GENTLE_G      1.5f  // Gentle shake → play
#define SHAKE_HARD_G        3.0f  // Hard shake → dizzy
#define SHAKE_GENTLE_MS     100   // Must sustain for 100 ms
#define SHAKE_HARD_MS       150   // Must sustain for 150 ms

// --- Demo-speed evolution thresholds (seconds) --------------------------------
// Multiply by 60 for real-world minutes (e.g. 30s demo = 30min real)
#define EVO_EGG_TO_BABY_S   30
#define EVO_BABY_TO_CHILD_S 120
#define EVO_CHILD_TO_TEEN_S 600
#define EVO_TEEN_TO_ADULT_S 1800
#define EVO_ADULT_TO_SENIOR_S 5400
#define EVO_SENIOR_LIFE_S   10800

// --- P1 incident tuning ------------------------------------------------------
#define P1_SPAWN_INTERVAL_S     60    // seconds of age between P1 spawns
#define P1_HEALTH_DRAIN         1     // health lost per game tick while P1 is active

// --- Vital stat tuning -------------------------------------------------------
#define HUNGER_DECAY_PER_TICK    1   // -1/tick while awake
#define HAPPINESS_DECAY_PER_TICK 1   // -1/tick while awake
#define SICK_HEALTH_DECAY        2   // Extra health drain when sick
#define ALERT_HUNGER_THRESH      20  // Below this → alert
#define ALERT_HAPPY_THRESH       20  // Below this → alert
#define ALERT_TIMEOUT_MS    120000UL // 2 min ignored → care mistake
#define SICK_HUNGER_THRESH       15  // Below this for long → sick
#define DIZZY_DURATION_MS   2000UL   // Hard shake dizzy lasts 2 s

// --- Evolution quality thresholds (care mistakes) ----------------------------
#define EVO_QUALITY_EXCELLENT    1   // 0-1 mistakes → excellent
#define EVO_QUALITY_GOOD         3   // 2-3 mistakes → good
// 4+ mistakes → tired/poor variant

// --- WiFi (compile-time fallback if no config.json) --------------------------
#define WIFI_SSID_DEFAULT       "tamagrotchi"
#define WIFI_PASS_DEFAULT       "changeme"
#define WIFI_CONNECT_TIMEOUT_MS 10000UL
#define WIFI_RETRY_INTERVAL_MS  30000UL

// --- OTLP fallback URL (overridden by config.json) ---------------------------
#define OTLP_BASE_URL_DEFAULT   "https://otlp-gateway-prod-eu-west-0.grafana.net/otlp"
#define OTLP_AUTH_DEFAULT       "Basic REPLACE_ME"
#define OTLP_DEVICE_ID_DEFAULT  "grot-1"

// --- NTP ---------------------------------------------------------------------
#define NTP_SERVER1     "pool.ntp.org"
#define NTP_SERVER2     "time.google.com"
#define NTP_GMT_OFFSET  0    // UTC; adjust for local time
#define NTP_DST_OFFSET  0
