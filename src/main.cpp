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
#include "game/mini_game_metric_catcher.h"
#include "sprites/sprite_engine.h"
#include "sprites/grot_frames.h"
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
// UI state machine
// =============================================================================
enum class UiMode : uint8_t {
    IDLE,        // pet visible, no icon selected
    NAVIGATING,  // icon bar active, A/C scroll, B select
    SUB_STATUS,  // stats overlay, auto-close 4s or B
    SUB_FEED,    // food choice overlay, A/C toggle, B execute
    SUB_LIGHTS,  // lights confirm overlay, B=toggle A=cancel
    SUB_DISMISS, // generic text overlay, any key or 2s auto-dismiss
    MINI_GAME,   // metric catcher mini-game running
};

static UiMode   g_ui_mode      = UiMode::IDLE;
static MenuItem g_menu_item    = MenuItem::STATUS;
static int      g_feed_choice  = 0;           // 0=Microchip, 1=SIN-wave
static uint32_t g_sub_enter_ms = 0;
static bool     g_sprite_test  = false;       // true while B-cycle test is active

// =============================================================================
// game_log() / game_trace() — strong overrides for the __attribute__((weak))
// stubs in game_engine.cpp.  Routes telemetry to Serial + OTLP buffers.
// =============================================================================
void game_log(uint8_t level, const char* event, const char* msg) {
    Serial.printf("[%s] %s\n", event, msg);
    otlp_log(level, event, msg);
}

void game_trace(const char* name, const char* body, uint32_t dur_ms) {
    otlp_trace(name, body, dur_ms);
}

// =============================================================================
// setup()
// =============================================================================
void setup() {
    // 0. CPU frequency — 80 MHz is sufficient for a 1 Hz game loop.
    //    APB clock stays at 80 MHz at this setting so SPI/I2C are unaffected.
    setCpuFrequencyMhz(80);

    // 0b. Power latch — GPIO41 HIGH keeps the board on; GPIO40 is a sense input
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
    lvgl_port_init(g_cfg.invert_colors);   // init LovyanGFX + LVGL
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
enum class TelemetryPhase : uint8_t { WAKING, PUSHING, SLEEPING, ASLEEP };
static TelemetryPhase _telem_phase   = TelemetryPhase::WAKING;  // start connected for NTP
static uint32_t _last_push_ms        = 0;
static uint32_t _last_sample_ms      = 0;
static uint32_t _wake_start_ms       = 0;
static bool     _rtc_synced          = false;   // NTP → RTC written once
static bool     _dead_shown          = false;   // death screen shown once
static bool     _was_sleeping        = false;   // tracks last pet sleep state for backlight

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

    // 8. UI state machine — icon menu navigation + action dispatch -------------
    {
        ButtonEvent evt = buttons_get_event();

        // Stop the mini-game immediately if the pet dies mid-game
        if (pet_is_dead(&g_pet) && g_ui_mode == UiMode::MINI_GAME) {
            metric_catcher_stop();
            g_ui_mode = UiMode::IDLE;
            ui_menu_set_selected(MenuItem::MENU_COUNT);
            ui_set_hints("", "", "");
        }

        // Dead screen: B press restarts the pet
        if (pet_is_dead(&g_pet)) {
            if (evt == ButtonEvent::B_PRESS) {
                pet_state_init(&g_pet);
                game_engine_init(&g_pet);
                _dead_shown = false;
                ui_show_game();
                ui_hide_overlay();
                g_ui_mode = UiMode::IDLE;
                ui_menu_set_selected(MenuItem::MENU_COUNT);
                ui_set_hints("", "", "");
            }
        } else {
            switch (g_ui_mode) {

            // ------------------------------------------------------------------
            case UiMode::IDLE:
                // A or C wakes the icon bar; exit sprite test mode
                if (evt == ButtonEvent::A_PRESS || evt == ButtonEvent::C_PRESS) {
                    g_sprite_test = false;
                    g_ui_mode = UiMode::NAVIGATING;
                    ui_menu_set_selected(g_menu_item);
                    ui_set_hints("[A] Prev", "[B] Select", "[C] Next");
                }
                // B cycles through sprite animation states (dev testing)
                if (evt == ButtonEvent::B_PRESS) {
                    g_sprite_test = true;
                    sprite_engine_test_next();
                }
                break;

            // ------------------------------------------------------------------
            case UiMode::NAVIGATING: {
                const int n = (int)MenuItem::MENU_COUNT;
                if (evt == ButtonEvent::A_PRESS) {
                    g_menu_item = (MenuItem)(((int)g_menu_item - 1 + n) % n);
                    ui_menu_set_selected(g_menu_item);
                } else if (evt == ButtonEvent::C_PRESS) {
                    g_menu_item = (MenuItem)(((int)g_menu_item + 1) % n);
                    ui_menu_set_selected(g_menu_item);
                } else if (evt == ButtonEvent::B_PRESS) {
                    // Activate selected icon
                    switch (g_menu_item) {
                    case MenuItem::STATUS:
                        g_ui_mode      = UiMode::SUB_STATUS;
                        g_sub_enter_ms = now;
                        ui_show_overlay_status(&g_pet, battery_read_voltage(),
                                               otlp_get_game_id());
                        ui_set_hints("", "[B] Close", "");
                        break;
                    case MenuItem::FEED:
                        g_ui_mode     = UiMode::SUB_FEED;
                        g_feed_choice = 0;
                        ui_show_overlay_feed(g_feed_choice);
                        ui_set_hints("[A] Prev", "[B] Feed!", "[C] Next");
                        break;
                    case MenuItem::CLEAN:
                        if (g_pet.hasP1) {
                            otlp_trace_begin("user.clean", "action=clean", 600);
                            action_clean(&g_pet);
                            otlp_trace_end();
                            g_ui_mode      = UiMode::SUB_DISMISS;
                            g_sub_enter_ms = now;
                            ui_show_overlay_text("P1 Resolved!\n\nHealth +10");
                        } else {
                            g_ui_mode      = UiMode::SUB_DISMISS;
                            g_sub_enter_ms = now;
                            ui_show_overlay_text("Nothing to clean\n\nData stream is clear");
                        }
                        ui_set_hints("", "[B] OK", "");
                        break;
                    case MenuItem::GAME:
                        metric_catcher_start(&g_pet);
                        g_ui_mode = UiMode::MINI_GAME;
                        ui_menu_set_selected(MenuItem::MENU_COUNT);
                        ui_set_hints("[A] Left", "[B] Quit", "[C] Right");
                        break;
                    case MenuItem::LIGHTS:
                        g_ui_mode      = UiMode::SUB_LIGHTS;
                        g_sub_enter_ms = now;
                        ui_show_overlay_text(g_pet.isSleeping
                            ? "Wake Grot up?\n\n[B] Wake  [A] Cancel"
                            : "Put Grot to sleep?\n\n[B] Sleep  [A] Cancel");
                        ui_set_hints("[A] Cancel", "[B] Confirm", "");
                        break;
                    case MenuItem::MEDICINE: {
                        bool was_sick = (g_pet.status == PetStatus::SICK);
                        otlp_trace_begin("user.medicine", "action=medicine", 600);
                        action_medicine(&g_pet);
                        otlp_trace_end();
                        g_ui_mode      = UiMode::SUB_DISMISS;
                        g_sub_enter_ms = now;
                        ui_show_overlay_text(was_sick
                            ? "Medicine given!\nHealth restored"
                            : "Medicine given");
                        ui_set_hints("", "[B] OK", "");
                        break;
                    }
                    default: break;
                    }
                } else if (evt == ButtonEvent::B_LONG) {
                    // Long-press B cancels navigation
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_hide_overlay();
                    ui_set_hints("", "", "");
                }
                break;
            }

            // ------------------------------------------------------------------
            case UiMode::SUB_STATUS:
                // Auto-close after 4s or on B press
                if (evt == ButtonEvent::B_PRESS ||
                    (now - g_sub_enter_ms) >= 4000UL) {
                    ui_hide_overlay();
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                }
                break;

            // ------------------------------------------------------------------
            case UiMode::SUB_FEED:
                if (evt == ButtonEvent::A_PRESS || evt == ButtonEvent::C_PRESS) {
                    g_feed_choice ^= 1;  // toggle 0↔1
                    ui_show_overlay_feed(g_feed_choice);
                } else if (evt == ButtonEvent::B_PRESS) {
                    FoodType food = (g_feed_choice == 0)
                                  ? FoodType::MICROCHIP
                                  : FoodType::SIN_WAVE;
                    otlp_trace_begin("user.feed",
                        g_feed_choice == 0 ? "food=microchip" : "food=sin_wave", 600);
                    action_feed(&g_pet, food);
                    otlp_trace_end();
                    ui_hide_overlay();
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                } else if (evt == ButtonEvent::B_LONG) {
                    ui_hide_overlay();
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                }
                break;

            // ------------------------------------------------------------------
            case UiMode::SUB_LIGHTS:
                if (evt == ButtonEvent::B_PRESS) {
                    if (g_pet.isSleeping) {
                        action_wake(&g_pet, rtc_get_hour());
                    } else {
                        g_pet.isSleeping = true;
                        g_pet.status     = PetStatus::SLEEPING;
                        game_log(9, "sleeping", "manual sleep via lights menu");
                    }
                    ui_hide_overlay();
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                } else if (evt == ButtonEvent::A_PRESS || evt == ButtonEvent::B_LONG) {
                    ui_hide_overlay();
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                }
                break;

            // ------------------------------------------------------------------
            case UiMode::SUB_DISMISS:
                // Any button press or 2s auto-dismiss
                if (evt != ButtonEvent::NONE ||
                    (now - g_sub_enter_ms) >= 2000UL) {
                    ui_hide_overlay();
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                }
                break;

            // ------------------------------------------------------------------
            case UiMode::MINI_GAME:
                metric_catcher_handle_input(evt);
                if (!metric_catcher_is_running()) {
                    g_ui_mode = UiMode::IDLE;
                    ui_menu_set_selected(MenuItem::MENU_COUNT);
                    ui_set_hints("", "", "");
                }
                break;
            }
        }
    }

    // 9. Shake interactions ---------------------------------------------------
    ShakeLevel shake = imu_get_shake();
    if (shake != ShakeLevel::NONE && !pet_is_dead(&g_pet)) {
        float mag = imu_get_accel_mag();

        if (shake == ShakeLevel::HARD) {
            // Hard shake → dizzy, lose 5 happiness
            char shake_body[48];
            snprintf(shake_body, sizeof(shake_body), "level=hard | accel_mag=%.2f", (double)mag);
            otlp_trace_begin("sensor.shake", shake_body, 2000);
            action_dizzy(&g_pet, mag);
            otlp_trace_end();
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
                char shake_body[48];
                snprintf(shake_body, sizeof(shake_body), "level=gentle | accel_mag=%.2f", (double)mag);
                otlp_trace_begin("sensor.shake", shake_body, 1000);
                game_trace("pet.shake_play", msg, 0);
                action_play(&g_pet);
                otlp_trace_end();
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
        char evo_body[32];
        snprintf(evo_body, sizeof(evo_body), "from=%s", life_stage_name(g_pet.stage));
        otlp_trace_begin("lifecycle.evolve", evo_body, 3000);
        evolution_advance(&g_pet);   // advances stage, computes quality, clears evolveReady
        otlp_trace_end();
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
    // Skip sprite state update while the B-cycle dev test is active or the
    // mini-game is running (both manage the sprite state themselves).
    if (!g_sprite_test && g_ui_mode != UiMode::MINI_GAME) {
        sprite_engine_set_state(g_pet.stage,
                                game_engine_get_emotion(&g_pet),
                                g_pet.quality);
    }

    // 12b. Sleep-aware backlight — dim when pet sleeps, restore on wake ------
    if (g_pet.isSleeping != _was_sleeping) {
        _was_sleeping = g_pet.isSleeping;
        lvgl_port_set_brightness(g_pet.isSleeping ? LCD_BL_SLEEP : LCD_BL_NORMAL);
    }

    // 13. Telemetry: sample metrics into buffer every sample_interval_s -------
    uint32_t sample_interval_ms = g_cfg.sample_interval_s * 1000UL;
    if ((now - _last_sample_ms) >= sample_interval_ms) {
        _last_sample_ms = now;
        otlp_sample_metrics(&g_pet, imu_get_accel_mag(), battery_read_voltage());
    }

    // 13b. WiFi sleep/wake FSM — batch push once per push_interval_s ---------
    uint32_t push_interval_ms = g_cfg.push_interval_s * 1000UL;

    switch (_telem_phase) {

    case TelemetryPhase::ASLEEP:
        if ((now - _last_push_ms) >= push_interval_ms) {
            _wake_start_ms = now;
            wifi_manager_wake();
            _telem_phase = TelemetryPhase::WAKING;
        }
        break;

    case TelemetryPhase::WAKING:
        if (wifi_manager_is_connected()) {
            otlp_schedule_push(&g_pet, imu_get_accel_mag(), WiFi.RSSI(),
                               battery_read_voltage());
            _telem_phase = TelemetryPhase::PUSHING;
        } else if ((now - _wake_start_ms) >= WIFI_WAKE_TIMEOUT_MS) {
            // Failed to connect — skip push, sleep, retry next window
            wifi_manager_sleep();
            _last_push_ms = now;
            _telem_phase  = TelemetryPhase::ASLEEP;
            game_log(13, "wifi_wake_timeout", "skipping push");
        }
        break;

    case TelemetryPhase::PUSHING:
        if (otlp_push_complete()) {
            ui_show_push_toast(otlp_last_push_ok());
            _last_push_ms = now;
            wifi_manager_sleep();
            _telem_phase = TelemetryPhase::SLEEPING;
        }
        break;

    case TelemetryPhase::SLEEPING:
        // Wait for WiFi stack to fully drop before declaring ASLEEP
        if (!wifi_manager_is_connected()) {
            _telem_phase = TelemetryPhase::ASLEEP;
        }
        break;
    }

    // 14. Yield — let the RTOS idle-sleep between iterations -----------------
    // At 10 ms the loop still drives LVGL at 100 calls/s (above the 50 ms
    // move-timer cadence) while freeing CPU cycles the chip would otherwise
    // burn in busy-wait.
    delay(LOOP_IDLE_MS);
}
