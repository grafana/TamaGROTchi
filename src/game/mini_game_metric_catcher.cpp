#include "mini_game_metric_catcher.h"
#include "../sprites/sprite_engine.h"
#include "../display/ui_screens.h"
#include "../telemetry/otlp_writer.h"
#include "../game/actions.h"
#include <Arduino.h>
#include <lvgl.h>

extern void game_log(uint8_t level, const char* event, const char* msg);

// =============================================================================
// Layout constants (sprite-zone-relative coordinates)
// Sprite zone: 240 × 178 px (y = 36–214 on screen)
// =============================================================================
static const int   BLOB_W         = 46;
static const int   BLOB_H         = 18;
static const int   NUM_BLOBS      = 5;
static const int   NUM_SLOTS      = 4;
static const int   GAME_SECS      = 30;
static const int   HUD_H          = 20;   // HUD strip at top of sprite zone
static const int   CATCH_Y        = 130;  // blob.bottom >= CATCH_Y → in catch zone
static const int   MISS_Y         = 178;  // blob.y >= MISS_Y → exited bottom
static const float SPEED_INIT     = 1.2f; // px per 100 ms tick
static const float SPEED_FAST     = 2.0f; // px per 100 ms tick (after 15 s)
// Slot centres (sprite-zone x).  Blob obj x = SLOT_X[s] - BLOB_W/2.
// Grot obj  x = SLOT_X[s] - 16  (sprite pivot at 16; rendered centre = obj_x + 16)
static const int   SLOT_X[4]      = { 28, 86, 144, 200 };
// Collision: |SLOT_X[blob] - SLOT_X[grot]| <= HALF_W_SUM
//            23 (blob half) + 48 (grot rendered half at 3× scale) = 71
static const int   HALF_W_SUM     = 71;
static const int   ANOMALY_CHANCE = 5;    // 1-in-5 blobs are anomalous

static const char* METRIC_NAMES[] = {
    "cpu_use", "err_rate", "lat_p99", "req_rt",
    "mem_use", "disk_io", "p95_resp", "gc_pause",
    "conn_pool", "q_len"
};
static const int NUM_METRICS = 10;

// =============================================================================
// Types
// =============================================================================
enum class GamePhase : uint8_t { COUNTDOWN, PLAYING, RESULTS };

struct Blob {
    lv_obj_t* obj;
    lv_obj_t* lbl;
    bool      active;
    bool      anomalous;
    int       slot;
    float     y;
    float     speed;
};

// =============================================================================
// Static state
// =============================================================================
static bool        _running        = false;
static PetState*   _pet            = nullptr;
static GamePhase   _phase          = GamePhase::COUNTDOWN;
static lv_timer_t* _timer          = nullptr;

static int         _score          = 0;
static int         _lives          = 3;
static int         _caught         = 0;
static int         _missed         = 0;
static int         _grot_slot      = 1;   // 0–3

static uint32_t    _start_ms       = 0;
static uint32_t    _phase_enter_ms = 0;
static uint32_t    _last_spawn_ms  = 0;
static uint32_t    _spawn_itvl_ms  = 1500;

static Blob        _blobs[NUM_BLOBS];

static lv_obj_t*   _hud_lives     = nullptr;
static lv_obj_t*   _hud_timer     = nullptr;
static lv_obj_t*   _hud_score     = nullptr;
static lv_obj_t*   _countdown_lbl = nullptr;

// =============================================================================
// Internal helpers
// =============================================================================
static lv_obj_t* get_sprite_zone() {
    lv_obj_t* s = sprite_engine_get_obj();
    return s ? lv_obj_get_parent(s) : nullptr;
}

static void hud_refresh_lives() {
    if (!_hud_lives) return;
    const char* txt = _lives >= 3 ? "H H H" :
                      _lives == 2 ? "H H  " :
                      _lives == 1 ? "H    " : "     ";
    lv_label_set_text(_hud_lives, txt);
}

static void hud_refresh_score() {
    if (!_hud_score) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", _score);
    lv_label_set_text(_hud_score, buf);
}

static void hud_refresh_timer(int secs) {
    if (!_hud_timer) return;
    if (secs < 0) secs = 0;
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", secs);
    lv_label_set_text(_hud_timer, buf);
}

static void deactivate_blob(int i) {
    _blobs[i].active = false;
    if (_blobs[i].obj) lv_obj_add_flag(_blobs[i].obj, LV_OBJ_FLAG_HIDDEN);
}

static void spawn_blob() {
    // Find free blob slot
    int idx = -1;
    for (int i = 0; i < NUM_BLOBS; i++) {
        if (!_blobs[i].active) { idx = i; break; }
    }
    if (idx < 0) return;

    // Pick a slot; prefer one not occupied near the spawn edge
    int slot = random(0, NUM_SLOTS);
    for (int t = 0; t < 4; t++) {
        int c = random(0, NUM_SLOTS);
        bool taken = false;
        for (int i = 0; i < NUM_BLOBS; i++) {
            if (_blobs[i].active && _blobs[i].slot == c && _blobs[i].y < 40.0f) {
                taken = true; break;
            }
        }
        if (!taken) { slot = c; break; }
    }

    bool anomalous = (random(0, ANOMALY_CHANCE) == 0);
    uint32_t elapsed = millis() - _start_ms;
    float spd = (elapsed >= 15000) ? SPEED_FAST : SPEED_INIT;
    spd += (float)(random(-2, 3)) * 0.1f;  // ±0.2 variance

    _blobs[idx].active   = true;
    _blobs[idx].anomalous = anomalous;
    _blobs[idx].slot     = slot;
    _blobs[idx].y        = (float)HUD_H;
    _blobs[idx].speed    = spd;

    lv_color_t col = anomalous
        ? lv_color_make(0xFF, 0x50, 0x50)
        : lv_color_make(0x3B, 0xD4, 0x79);
    lv_obj_set_pos(_blobs[idx].obj,
                   (lv_coord_t)(SLOT_X[slot] - BLOB_W / 2),
                   (lv_coord_t)HUD_H);
    lv_obj_set_style_bg_color(_blobs[idx].obj, col, 0);
    lv_label_set_text(_blobs[idx].lbl, METRIC_NAMES[random(0, NUM_METRICS)]);
    lv_obj_clear_flag(_blobs[idx].obj, LV_OBJ_FLAG_HIDDEN);
}

// Hide all game widgets and show the results overlay text.
static void show_results_overlay(bool won, bool quit) {
    for (int i = 0; i < NUM_BLOBS; i++) deactivate_blob(i);
    if (_hud_lives) lv_obj_add_flag(_hud_lives, LV_OBJ_FLAG_HIDDEN);
    if (_hud_timer) lv_obj_add_flag(_hud_timer, LV_OBJ_FLAG_HIDDEN);
    if (_hud_score) lv_obj_add_flag(_hud_score, LV_OBJ_FLAG_HIDDEN);

    char text[80];
    if (quit) {
        snprintf(text, sizeof(text), "Quit\nScore: %d\n\n[B] OK", _score);
    } else if (won) {
        snprintf(text, sizeof(text),
                 "WIN!\nScore: %d\nCaught: %d\n\n[B] OK", _score, _caught);
    } else {
        snprintf(text, sizeof(text),
                 "GAME OVER\nScore: %d\nMissed: %d\n\n[B] OK", _score, _missed);
    }
    ui_show_overlay_text(text);

    _phase          = GamePhase::RESULTS;
    _phase_enter_ms = millis();
}

// Apply stat changes, send telemetry, then show results.
static void finish_game(bool won) {
    uint32_t elapsed_ms = millis() - _start_ms;

    if (won) {
        action_play(_pet);  // +20 happiness, clears alert
    } else {
        int h = (int)_pet->happiness - 10;
        _pet->happiness = (h < 0) ? 0 : (uint8_t)h;
    }

    char body[96];
    snprintf(body, sizeof(body),
             "outcome=%s | score=%d | caught=%d | missed=%d",
             won ? "win" : "lose", _score, _caught, _missed);
    otlp_trace_begin("minigame.metric_catcher", body, elapsed_ms);
    game_log(9, "minigame_end", body);
    otlp_trace_end();

    show_results_overlay(won, false);
}

// =============================================================================
// Game timer (100 ms tick)
// =============================================================================
static void game_timer_cb(lv_timer_t*) {
    if (!_running) return;
    uint32_t now = millis();

    switch (_phase) {

    // ------------------------------------------------------------------
    case GamePhase::COUNTDOWN: {
        uint32_t elapsed = now - _phase_enter_ms;
        int left = 3 - (int)(elapsed / 1000);
        if (_countdown_lbl) {
            if (left > 0) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%d", left);
                lv_label_set_text(_countdown_lbl, buf);
            } else {
                lv_label_set_text(_countdown_lbl, "GO!");
            }
        }
        if (elapsed >= 3000) {
            if (_countdown_lbl) lv_obj_add_flag(_countdown_lbl, LV_OBJ_FLAG_HIDDEN);
            _phase          = GamePhase::PLAYING;
            _start_ms       = now;
            _phase_enter_ms = now;
            _last_spawn_ms  = now;
            _spawn_itvl_ms  = (uint32_t)random(1000, 2500);
        }
        break;
    }

    // ------------------------------------------------------------------
    case GamePhase::PLAYING: {
        uint32_t elapsed  = now - _start_ms;
        int      secs_left = GAME_SECS - (int)(elapsed / 1000);
        hud_refresh_timer(secs_left);

        if (secs_left <= 0) {
            finish_game(true);
            return;
        }

        // Move every active blob; check catch / miss
        for (int i = 0; i < NUM_BLOBS; i++) {
            if (!_blobs[i].active) continue;

            _blobs[i].y += _blobs[i].speed;
            float bot = _blobs[i].y + BLOB_H;

            if (_blobs[i].y >= (float)MISS_Y) {
                // Blob exited bottom of play area
                if (!_blobs[i].anomalous) {
                    _lives--;
                    _missed++;
                    hud_refresh_lives();
                    char msg[48];
                    snprintf(msg, sizeof(msg), "slot=%d | lives=%d | score=%d",
                             _blobs[i].slot, _lives, _score);
                    game_log(13, "blob_missed", msg);
                } else {
                    // Anomalous blob successfully dodged
                    _score += 5;
                    hud_refresh_score();
                    char msg[48];
                    snprintf(msg, sizeof(msg), "slot=%d | score=%d",
                             _blobs[i].slot, _score);
                    game_log(9, "anomaly_dodged", msg);
                }
                deactivate_blob(i);
                if (_lives <= 0) { finish_game(false); return; }
                continue;
            }

            if (bot >= (float)CATCH_Y) {
                // In catch zone — horizontal collision check
                if (abs(SLOT_X[_blobs[i].slot] - SLOT_X[_grot_slot]) <= HALF_W_SUM) {
                    if (!_blobs[i].anomalous) {
                        _score += 10;
                        _caught++;
                        hud_refresh_score();
                        char msg[48];
                        snprintf(msg, sizeof(msg), "slot=%d | score=%d",
                                 _blobs[i].slot, _score);
                        game_log(9, "blob_caught", msg);
                    } else {
                        _lives--;
                        hud_refresh_lives();
                        char msg[48];
                        snprintf(msg, sizeof(msg), "slot=%d | lives=%d",
                                 _blobs[i].slot, _lives);
                        game_log(13, "anomaly_hit", msg);
                    }
                    deactivate_blob(i);
                    if (_lives <= 0) { finish_game(false); return; }
                    continue;
                }
            }

            // Blob still in flight — update visual position
            lv_obj_set_y(_blobs[i].obj, (lv_coord_t)_blobs[i].y);
        }

        // Spawn a new blob if interval elapsed
        if ((now - _last_spawn_ms) >= _spawn_itvl_ms) {
            spawn_blob();
            _last_spawn_ms = now;
            _spawn_itvl_ms = (uint32_t)random(1000, 2500);
        }
        break;
    }

    // ------------------------------------------------------------------
    case GamePhase::RESULTS:
        // Auto-dismiss after 5 s; B press handled in handle_input
        if ((now - _phase_enter_ms) >= 5000) {
            metric_catcher_stop();
        }
        break;
    }
}

// =============================================================================
// LVGL object lifecycle
// =============================================================================
static void create_game_objects() {
    lv_obj_t* zone = get_sprite_zone();
    if (!zone) return;

    // Blob pool — created first so HUD labels render on top
    for (int i = 0; i < NUM_BLOBS; i++) {
        lv_obj_t* pill = lv_obj_create(zone);
        lv_obj_set_size(pill, BLOB_W, BLOB_H);
        lv_obj_set_style_radius(pill, BLOB_H / 2, 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        lv_obj_set_style_pad_all(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* lbl = lv_label_create(pill);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
        lv_label_set_text(lbl, "");

        _blobs[i] = { pill, lbl, false, false, 0, 0.0f, SPEED_INIT };
    }

    // HUD — lives (left), timer (centre), score (right)
    _hud_lives = lv_label_create(zone);
    lv_obj_set_style_text_font(_hud_lives, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_hud_lives, lv_color_make(0xFF, 0x50, 0x50), 0);
    lv_obj_set_pos(_hud_lives, 2, 4);
    lv_label_set_text(_hud_lives, "H H H");

    _hud_timer = lv_label_create(zone);
    lv_obj_set_style_text_font(_hud_timer, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_hud_timer, lv_color_white(), 0);
    lv_obj_set_pos(_hud_timer, 100, 4);
    lv_label_set_text(_hud_timer, "30s");

    _hud_score = lv_label_create(zone);
    lv_obj_set_style_text_font(_hud_score, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_hud_score, lv_color_make(0x3B, 0xD4, 0x79), 0);
    lv_obj_set_pos(_hud_score, 200, 4);
    lv_label_set_text(_hud_score, "0");

    // Countdown label — centred, drawn on top of everything
    _countdown_lbl = lv_label_create(zone);
    lv_obj_set_style_text_font(_countdown_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_countdown_lbl, lv_color_make(0xFF, 0x7F, 0x00), 0);
    lv_obj_set_style_bg_color(_countdown_lbl, lv_color_make(0x11, 0x11, 0x1B), 0);
    lv_obj_set_style_bg_opa(_countdown_lbl, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(_countdown_lbl, 8, 0);
    lv_obj_center(_countdown_lbl);
    lv_label_set_text(_countdown_lbl, "3");
}

static void destroy_game_objects() {
    for (int i = 0; i < NUM_BLOBS; i++) {
        if (_blobs[i].obj) {
            lv_obj_del(_blobs[i].obj);
            _blobs[i].obj = nullptr;
            _blobs[i].lbl = nullptr;
        }
        _blobs[i].active = false;
    }
    if (_hud_lives)     { lv_obj_del(_hud_lives);     _hud_lives     = nullptr; }
    if (_hud_timer)     { lv_obj_del(_hud_timer);     _hud_timer     = nullptr; }
    if (_hud_score)     { lv_obj_del(_hud_score);     _hud_score     = nullptr; }
    if (_countdown_lbl) { lv_obj_del(_countdown_lbl); _countdown_lbl = nullptr; }
}

// =============================================================================
// Public API
// =============================================================================
void metric_catcher_start(PetState* pet) {
    if (_running) metric_catcher_stop();

    _pet            = pet;
    _running        = true;
    _phase          = GamePhase::COUNTDOWN;
    _phase_enter_ms = millis();
    _score          = 0;
    _lives          = 3;
    _caught         = 0;
    _missed         = 0;
    _grot_slot      = 1;

    // Suppress stat decay while the game runs
    pet->status = PetStatus::PLAYING;

    create_game_objects();

    // HAPPY emotion → sprite enters JUMP mode (y-only bounce)
    // The game then controls x freely each input event.
    sprite_engine_set_state(pet->stage, GrotEmotion::HAPPY, pet->quality);
    lv_obj_t* sobj = sprite_engine_get_obj();
    if (sobj) lv_obj_set_x(sobj, SLOT_X[_grot_slot] - 16);

    _timer = lv_timer_create(game_timer_cb, 100, nullptr);
}

void metric_catcher_stop() {
    if (!_running) return;
    _running = false;
    if (_timer) { lv_timer_del(_timer); _timer = nullptr; }
    destroy_game_objects();
    ui_hide_overlay();
}

bool metric_catcher_is_running() {
    return _running;
}

void metric_catcher_handle_input(ButtonEvent evt) {
    if (!_running) return;

    switch (_phase) {

    case GamePhase::PLAYING:
        if (evt == ButtonEvent::A_PRESS && _grot_slot > 0) {
            _grot_slot--;
            lv_obj_t* s = sprite_engine_get_obj();
            if (s) lv_obj_set_x(s, SLOT_X[_grot_slot] - 16);
        } else if (evt == ButtonEvent::C_PRESS && _grot_slot < NUM_SLOTS - 1) {
            _grot_slot++;
            lv_obj_t* s = sprite_engine_get_obj();
            if (s) lv_obj_set_x(s, SLOT_X[_grot_slot] - 16);
        } else if (evt == ButtonEvent::B_LONG) {
            // Quit early — no stat delta, just telemetry + results
            uint32_t elapsed_ms = millis() - _start_ms;
            char body[80];
            snprintf(body, sizeof(body),
                     "outcome=quit | score=%d | caught=%d | missed=%d",
                     _score, _caught, _missed);
            otlp_trace_begin("minigame.metric_catcher", body, elapsed_ms);
            game_log(9, "minigame_end", body);
            otlp_trace_end();
            show_results_overlay(false, true);
        }
        break;

    case GamePhase::RESULTS:
        if (evt == ButtonEvent::B_PRESS || evt == ButtonEvent::B_LONG) {
            metric_catcher_stop();
        }
        break;

    default:
        break;
    }
}
