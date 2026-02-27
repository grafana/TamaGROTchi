#include "ui_screens.h"
#include "../config.h"
#include "../sprites/sprite_engine.h"
#include <Arduino.h>

// =============================================================================
// Layout dimensions (must sum to LCD_HEIGHT = 280)
// =============================================================================
#define HEADER_H    30
#define SPRITE_H    170
#define BARS_H      22
#define LEGEND_H    20
#define HINTS_H     38
// Total: 30 + 170 + 22 + 20 + 38 = 280 ✓

// Grafana orange (used as accent)
#define GRAFANA_ORANGE  lv_color_make(0xFF, 0x7F, 0x00)
#define COL_HUNGER      lv_color_make(0xFF, 0x7F, 0x00)   // orange
#define COL_HAPPY       lv_color_make(0x3B, 0xD4, 0x79)   // Grafana green
#define COL_HEALTH      lv_color_make(0x5E, 0x9E, 0xFF)   // Grafana blue
#define COL_BG          lv_color_make(0x11, 0x11, 0x1B)   // dark background

// =============================================================================
// Widget handles
// =============================================================================
static lv_obj_t* _screen_game  = nullptr;
static lv_obj_t* _screen_dead  = nullptr;

// Header row
static lv_obj_t* _lbl_age      = nullptr;
static lv_obj_t* _lbl_wifi     = nullptr;

// Vitals panel
static lv_obj_t* _bar_hunger   = nullptr;
static lv_obj_t* _bar_happy    = nullptr;
static lv_obj_t* _bar_health   = nullptr;
static lv_obj_t* _lbl_hun_pct  = nullptr;
static lv_obj_t* _lbl_hap_pct  = nullptr;
static lv_obj_t* _lbl_hlt_pct  = nullptr;

// Evolve overlay
static lv_obj_t* _lbl_evolve   = nullptr;

// Toast
static lv_obj_t* _lbl_toast    = nullptr;
static lv_timer_t* _toast_timer = nullptr;

static void toast_hide_cb(lv_timer_t* t) {
    if (_lbl_toast) lv_obj_add_flag(_lbl_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(t);
    _toast_timer = nullptr;
}

// =============================================================================
// Style helpers
// =============================================================================
static lv_style_t _style_bg;
static lv_style_t _style_bar_bg;
static bool _styles_init = false;

static void init_styles() {
    if (_styles_init) return;
    _styles_init = true;

    lv_style_init(&_style_bg);
    lv_style_set_bg_color(&_style_bg, COL_BG);
    lv_style_set_border_width(&_style_bg, 0);
    lv_style_set_pad_all(&_style_bg, 0);
    lv_style_set_radius(&_style_bg, 0);

    lv_style_init(&_style_bar_bg);
    lv_style_set_bg_color(&_style_bar_bg, lv_color_make(0x33, 0x33, 0x44));
    lv_style_set_border_width(&_style_bar_bg, 0);
    lv_style_set_radius(&_style_bar_bg, 4);
}

static lv_obj_t* make_bar(lv_obj_t* parent, lv_color_t colour, lv_coord_t x, lv_coord_t w) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, BARS_H - 4);
    lv_obj_set_pos(bar, x, 2);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 80, LV_ANIM_OFF);

    // Style: dark background, coloured indicator
    lv_obj_add_style(bar, &_style_bar_bg, LV_PART_MAIN);

    static lv_style_t st_ind[3];
    static int st_cnt = 0;
    lv_style_init(&st_ind[st_cnt]);
    lv_style_set_bg_color(&st_ind[st_cnt], colour);
    lv_style_set_bg_opa(&st_ind[st_cnt], LV_OPA_COVER);
    lv_style_set_radius(&st_ind[st_cnt], 4);
    lv_obj_add_style(bar, &st_ind[st_cnt], LV_PART_INDICATOR);
    st_cnt = (st_cnt + 1) % 3;

    return bar;
}

static lv_obj_t* make_pct_label(lv_obj_t* parent, lv_coord_t x, lv_coord_t w) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "100%");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_pos(lbl, x, 4);
    lv_obj_set_width(lbl, w);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    return lbl;
}

// =============================================================================
// Build main game screen
// =============================================================================
static void build_game_screen() {
    init_styles();

    _screen_game = lv_obj_create(nullptr);
    lv_obj_add_style(_screen_game, &_style_bg, 0);

    // ---- Header row (y=0, h=30) ----
    lv_obj_t* header = lv_obj_create(_screen_game);
    lv_obj_set_size(header, LCD_WIDTH, HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_add_style(header, &_style_bg, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _lbl_age = lv_label_create(header);
    lv_label_set_text(_lbl_age, "Age: 0m");
    lv_obj_set_style_text_font(_lbl_age, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_age, lv_color_make(0xBB, 0xBB, 0xBB), 0);

    _lbl_wifi = lv_label_create(header);
    lv_label_set_text(_lbl_wifi, "WiFi");
    lv_obj_set_style_text_font(_lbl_wifi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x88, 0x88, 0x88), 0);

    // ---- Sprite zone (y=30, h=170) — sprite centred by sprite_engine ----
    // (sprite_engine_init places the image; we just reserve space)
    lv_obj_t* sprite_zone = lv_obj_create(_screen_game);
    lv_obj_set_size(sprite_zone, LCD_WIDTH, SPRITE_H);
    lv_obj_set_pos(sprite_zone, 0, HEADER_H);
    lv_obj_add_style(sprite_zone, &_style_bg, 0);
    lv_obj_clear_flag(sprite_zone, LV_OBJ_FLAG_SCROLLABLE);

    // Initialise the sprite engine, parenting sprites to the sprite zone
    sprite_engine_init(sprite_zone);

    // ---- Vitals panel (y=200, h=22) ----
    lv_obj_t* vitals = lv_obj_create(_screen_game);
    lv_obj_set_size(vitals, LCD_WIDTH, BARS_H);
    lv_obj_set_pos(vitals, 0, HEADER_H + SPRITE_H);
    lv_obj_add_style(vitals, &_style_bg, 0);
    lv_obj_set_style_pad_all(vitals, 0, 0);
    lv_obj_clear_flag(vitals, LV_OBJ_FLAG_SCROLLABLE);

    // Three bars — each ~78px wide with 2px gaps
    const lv_coord_t bar_w = (LCD_WIDTH - 6) / 3;  // ~78px
    _bar_hunger = make_bar(vitals, COL_HUNGER, 2,             bar_w);
    _bar_happy  = make_bar(vitals, COL_HAPPY,  2 + bar_w + 2, bar_w);
    _bar_health = make_bar(vitals, COL_HEALTH, 2 + (bar_w + 2) * 2, bar_w);

    // Percentage labels overlaid on bars
    _lbl_hun_pct = make_pct_label(vitals, 2,             bar_w);
    _lbl_hap_pct = make_pct_label(vitals, 2 + bar_w + 2, bar_w);
    _lbl_hlt_pct = make_pct_label(vitals, 2 + (bar_w + 2) * 2, bar_w);

    // ---- Legend row (y=222, h=20) ----
    lv_obj_t* legend = lv_obj_create(_screen_game);
    lv_obj_set_size(legend, LCD_WIDTH, LEGEND_H);
    lv_obj_set_pos(legend, 0, HEADER_H + SPRITE_H + BARS_H);
    lv_obj_add_style(legend, &_style_bg, 0);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_legend_lbl = [&](const char* text, lv_color_t col) {
        lv_obj_t* l = lv_label_create(legend);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(l, col, 0);
    };
    make_legend_lbl("Hunger",    COL_HUNGER);
    make_legend_lbl("Happiness", COL_HAPPY);
    make_legend_lbl("Health",    COL_HEALTH);

    // ---- Button hints (y=242, h=38) ----
    lv_obj_t* hints = lv_obj_create(_screen_game);
    lv_obj_set_size(hints, LCD_WIDTH, HINTS_H);
    lv_obj_set_pos(hints, 0, HEADER_H + SPRITE_H + BARS_H + LEGEND_H);
    lv_obj_add_style(hints, &_style_bg, 0);
    lv_obj_set_flex_flow(hints, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hints, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_hint_lbl = [&](const char* text) {
        lv_obj_t* l = lv_label_create(hints);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_make(0x88, 0x88, 0x88), 0);
    };
    make_hint_lbl("[A] Feed");
    make_hint_lbl("[B] Menu");
    make_hint_lbl("[C] Play");

    // ---- Evolve overlay (hidden by default) ----
    _lbl_evolve = lv_label_create(_screen_game);
    lv_label_set_text(_lbl_evolve, "EVOLVING...");
    lv_obj_set_style_text_font(_lbl_evolve, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_evolve, GRAFANA_ORANGE, 0);
    lv_obj_center(_lbl_evolve);
    lv_obj_add_flag(_lbl_evolve, LV_OBJ_FLAG_HIDDEN);

    // ---- Toast label (hidden by default) ----
    _lbl_toast = lv_label_create(_screen_game);
    lv_label_set_text(_lbl_toast, "");
    lv_obj_set_style_text_font(_lbl_toast, &lv_font_montserrat_10, 0);
    lv_obj_align(_lbl_toast, LV_ALIGN_TOP_RIGHT, -4, 32);
    lv_obj_add_flag(_lbl_toast, LV_OBJ_FLAG_HIDDEN);
}

static void build_dead_screen() {
    _screen_dead = lv_obj_create(nullptr);
    lv_obj_add_style(_screen_dead, &_style_bg, 0);

    lv_obj_t* lbl = lv_label_create(_screen_dead);
    lv_label_set_text(lbl, "GROT HAS LEFT\nTHE DASHBOARD\n\nPress [B] to restart");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC, 0x33, 0x33), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
}

// =============================================================================
// Public API
// =============================================================================
void ui_screens_init() {
    init_styles();
    build_game_screen();
    build_dead_screen();
}

void ui_show_game() {
    lv_screen_load(_screen_game);
}

void ui_show_dead() {
    lv_screen_load(_screen_dead);
}

void ui_update_vitals(const PetState* p) {
    if (!_bar_hunger) return;

    lv_bar_set_value(_bar_hunger, p->hunger,    LV_ANIM_OFF);
    lv_bar_set_value(_bar_happy,  p->happiness, LV_ANIM_OFF);
    lv_bar_set_value(_bar_health, p->health,    LV_ANIM_OFF);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", p->hunger);
    lv_label_set_text(_lbl_hun_pct, buf);
    snprintf(buf, sizeof(buf), "%d%%", p->happiness);
    lv_label_set_text(_lbl_hap_pct, buf);
    snprintf(buf, sizeof(buf), "%d%%", p->health);
    lv_label_set_text(_lbl_hlt_pct, buf);
}

void ui_update_header(const PetState* p, bool wifi_connected) {
    if (!_lbl_age) return;

    // Format age
    char age_buf[32];
    uint32_t mins  = p->ageSeconds / 60;
    uint32_t hours = mins / 60;
    mins %= 60;
    if (hours > 0) snprintf(age_buf, sizeof(age_buf), "Age: %luh %lum", hours, mins);
    else           snprintf(age_buf, sizeof(age_buf), "Age: %lum", mins);
    lv_label_set_text(_lbl_age, age_buf);

    // WiFi indicator
    if (wifi_connected) {
        lv_label_set_text(_lbl_wifi, "WiFi \xE2\x9C\x93");  // UTF-8 checkmark
        lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x3B, 0xD4, 0x79), 0);
    } else {
        lv_label_set_text(_lbl_wifi, "WiFi...");
        lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x88, 0x88, 0x88), 0);
    }
}

void ui_show_push_toast(bool success) {
    if (!_lbl_toast) return;
    lv_label_set_text(_lbl_toast, success ? "Grafana OK" : "Push fail");
    lv_obj_set_style_text_color(_lbl_toast,
        success ? lv_color_make(0x3B, 0xD4, 0x79) : lv_color_make(0xFF, 0x40, 0x40), 0);
    lv_obj_clear_flag(_lbl_toast, LV_OBJ_FLAG_HIDDEN);

    // Auto-hide after 2 s
    if (_toast_timer) lv_timer_del(_toast_timer);
    _toast_timer = lv_timer_create(toast_hide_cb, 2000, nullptr);
    lv_timer_set_repeat_count(_toast_timer, 1);
}

void ui_show_evolve_overlay(bool visible) {
    if (!_lbl_evolve) return;
    if (visible) lv_obj_clear_flag(_lbl_evolve, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(_lbl_evolve,   LV_OBJ_FLAG_HIDDEN);
}
