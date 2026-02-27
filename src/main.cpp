// =============================================================================
// Tamagrotchi — Entry point
// Waveshare ESP32-S3-Touch-LCD-1.69 (ST7789V2, QMI8658C, PCF85063A)
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "config_mgr/config_manager.h"
#include "display/lvgl_port.h"
#include "display/ui_screens.h"
#include "input/buttons.h"
#include "buzzer/buzzer.h"
#include "game/pet_state.h"
#include "game/game_engine.h"
#include "game/evolution.h"
#include "game/actions.h"
#include "sprites/sprite_engine.h"
#include "wifi/wifi_manager.h"
#include "telemetry/otlp_writer.h"
#include "imu/imu_driver.h"
#include "rtc/rtc_driver.h"
#include "power/battery.h"

// =============================================================================
// Globals
// =============================================================================
static PetState  g_pet;
static AppConfig g_cfg;

// =============================================================================
// game_log() — strong override for the __attribute__((weak)) stub in
// game_engine.cpp.  Routes all structured logs to Serial + OTLP buffer.
// =============================================================================
void game_log(uint8_t level, const char* event, const char* msg) {
    Serial.printf("[%s] %s\n", event, msg);
    otlp_log(level, event, msg);
}

// =============================================================================
// setup()
// =============================================================================
void setup() {
    // 0. Power latch — GPIO41 HIGH keeps the board on; GPIO40 is a sense input
    pinMode(PIN_SYS_EN,  OUTPUT);
    digitalWrite(PIN_SYS_EN,  HIGH);
    pinMode(PIN_SYS_OUT, INPUT);   // power sense — do NOT drive this pin

    Serial.begin(115200);
    // ESP32-S3 USB CDC needs time to re-enumerate after the boot reset.
    // Wait up to 2 s for the host to connect; fall through anyway so the
    // device doesn't hang when no PC is attached.
    { uint32_t t = millis(); while (!Serial && (millis() - t) < 2000) delay(10); }
    delay(100);
    Serial.println("\n[main] === Tamagrotchi boot ===");

    // 1. Load runtime config from LittleFS /config.json ----------------------
    config_manager_load(&g_cfg);
    config_manager_print(&g_cfg);

    // 2. Display + LVGL -------------------------------------------------------
    lvgl_port_init();          // init LovyanGFX + LVGL
    ui_screens_init();         // build all LVGL screens (also inits sprite engine)
    ui_show_game();            // activate main game screen

    // 3. Input ----------------------------------------------------------------
    buttons_init();

    // 4. Audio ----------------------------------------------------------------
    buzzer_init();
    buzzer_set_muted(!g_cfg.buzzer_enabled);
    if (g_cfg.flash_alerts)
        buzzer_set_play_cb([]() { display_alert_flash(2); });
    buzzer_play_async(MELODY_BOOT, MELODY_BOOT_LEN);

    // 5. IMU + RTC (Wire.begin called inside imu_driver_init) -----------------
    imu_driver_init();
    rtc_driver_init();
    battery_init();

    // 6. Pet state + game engine ----------------------------------------------
    pet_state_init(&g_pet);
    game_engine_init(&g_pet);

    // 7. WiFi (non-blocking) + telemetry writer -------------------------------
    wifi_manager_init(g_cfg.wifi_ssid, g_cfg.wifi_password);
    otlp_writer_init(g_cfg.otlp_base, g_cfg.auth_b64, g_cfg.device_id);
    otlp_writer_start_task();   // background push task on Core 0

    // 8. Boot log (buffered; flushed on first successful WiFi push) -----------
    char boot_msg[80];
    snprintf(boot_msg, sizeof(boot_msg),
             "stage=%s | firmware=1.0.0", life_stage_name(g_pet.stage));
    game_log(9 /*INFO*/, "boot", boot_msg);

    Serial.println("[main] Setup complete\n");
}

// =============================================================================
// loop()
// =============================================================================
static uint32_t _last_push_ms = 0;
static bool     _rtc_synced   = false;   // NTP → RTC written once
static bool     _dead_shown   = false;   // death screen shown once

void loop() {
    uint32_t now = millis();

    // 1. LVGL heartbeat -------------------------------------------------------
    lvgl_port_tick();

    // 2. Advance async buzzer melody ------------------------------------------
    buzzer_update();

    // 3. Debounce buttons, fill event ring buffer ------------------------------
    buttons_update();

    // 4. IMU read ~10 Hz -------------------------------------------------------
    static uint32_t _imu_ms = 0;
    if (now - _imu_ms >= 100) {
        _imu_ms = now;
        imu_driver_update();
    }

    // 5. Non-blocking WiFi FSM ------------------------------------------------
    wifi_manager_update();

    // 6. Sync NTP time back to hardware RTC (once, after SNTP settles) --------
    if (!_rtc_synced) {
        time_t t = time(nullptr);
        if (t > 1000000000L) {
            rtc_driver_sync_from_ntp(t);
            _rtc_synced = true;
        }
    }

    // 7. Game tick (internally rate-limited to GAME_TICK_MS) ------------------
    game_engine_update(&g_pet);

    // 8. Button events → player actions ---------------------------------------
    if (!pet_is_dead(&g_pet)) {
        ButtonEvent evt = buttons_get_event();
        switch (evt) {
        case ButtonEvent::A_PRESS: action_feed(&g_pet);       break;  // A = Feed
        case ButtonEvent::C_PRESS: action_play(&g_pet);       break;  // C = Play
        case ButtonEvent::B_PRESS: action_discipline(&g_pet); break;  // B = Discipline
        case ButtonEvent::B_LONG:  action_medicine(&g_pet);   break;  // B (hold) = Medicine
        default: break;
        }
    }

    // 9. Shake interactions ---------------------------------------------------
    ShakeLevel shake = imu_get_shake();
    if (shake != ShakeLevel::NONE && !pet_is_dead(&g_pet)) {
        float mag = imu_get_accel_mag();

        if (shake == ShakeLevel::HARD) {
            // Hard shake → dizzy, lose 5 happiness
            action_dizzy(&g_pet, mag);
        } else {
            // Gentle shake
            if (g_pet.isSleeping) {
                // Wake early → care mistake if before wakeHour
                action_wake(&g_pet, rtc_get_hour());
            } else {
                char msg[48];
                snprintf(msg, sizeof(msg),
                         "accel_mag=%.2f | action=play", (double)mag);
                game_log(9, "shake_play", msg);
                action_play(&g_pet);
            }
        }
    }

    // 10. Evolution overlay ---------------------------------------------------
    if (g_pet.evolveReady &&
        g_pet.status != PetStatus::EVOLVING &&
        g_pet.status != PetStatus::DEAD) {
        g_pet.status        = PetStatus::EVOLVING;
        g_pet.statusStartMs = now;
        ui_show_evolve_overlay(true);
        buzzer_play_async(MELODY_EVOLVE, MELODY_EVOLVE_LEN);
    }
    // Complete the transition after the 3-second animation
    if (g_pet.status == PetStatus::EVOLVING &&
        (now - g_pet.statusStartMs) >= 3000UL) {
        ui_show_evolve_overlay(false);
        evolution_advance(&g_pet);   // advances stage, computes quality, clears evolveReady
    }

    // 11. Death screen (shown once) -------------------------------------------
    if (pet_is_dead(&g_pet) && !_dead_shown) {
        _dead_shown = true;
        ui_show_dead();
        buzzer_play_async(MELODY_DEAD, MELODY_DEAD_LEN);
    }

    // 12. Refresh UI ----------------------------------------------------------
    ui_update_vitals(&g_pet);
    ui_update_header(&g_pet, wifi_manager_is_connected());
    sprite_engine_set_state(g_pet.stage,
                            game_engine_get_emotion(&g_pet),
                            g_pet.quality);

    // 13. Telemetry push on configured interval (non-blocking) ---------------
    uint32_t push_interval_ms = g_cfg.push_interval_s * 1000UL;
    if (wifi_manager_is_connected() &&
        (now - _last_push_ms) >= push_interval_ms) {
        _last_push_ms = now;
        otlp_schedule_push(&g_pet, imu_get_accel_mag(), WiFi.RSSI(),
                           battery_read_voltage());
    }

    // Show push toast when background task reports completion
    if (otlp_push_complete()) {
        ui_show_push_toast(otlp_last_push_ok());
    }
}
