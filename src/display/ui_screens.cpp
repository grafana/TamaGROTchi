#include "ui_screens.h"
#include "../config.h"
#include "../sprites/sprite_engine.h"
#include <Arduino.h>

// =============================================================================
// Layout dimensions (must sum to LCD_HEIGHT = 280)
// =============================================================================
#define ICON_BAR_H  36
#define SPRITE_H    178
#define BARS_H      22
#define LEGEND_H    16
#define HINTS_H     28
// Total: 36 + 178 + 22 + 16 + 28 = 280 ✓

// Colours
#define GRAFANA_ORANGE  lv_color_make(0xFF, 0x7F, 0x00)
#define COL_HUNGER      lv_color_make(0xFF, 0x7F, 0x00)
#define COL_HAPPY       lv_color_make(0x3B, 0xD4, 0x79)
#define COL_HEALTH      lv_color_make(0x5E, 0x9E, 0xFF)
#define COL_BG          lv_color_make(0x11, 0x11, 0x1B)
#define COL_ICON_DIM    lv_color_make(0x66, 0x66, 0x77)
#define COL_DIVIDER     lv_color_make(0x33, 0x33, 0x44)

// LVGL symbol for each MenuItem (same order as enum)
static const char* const MENU_SYMBOLS[(int)MenuItem::MENU_COUNT] = {
    LV_SYMBOL_LIST,    // STATUS
    LV_SYMBOL_PLUS,    // FEED
    LV_SYMBOL_TRASH,   // CLEAN
    LV_SYMBOL_PLAY,    // GAME
    LV_SYMBOL_POWER,   // LIGHTS
    LV_SYMBOL_CHARGE,  // MEDICINE
};

// =============================================================================
// Widget handles
// =============================================================================
static lv_obj_t* _screen_game = nullptr;
static lv_obj_t* _screen_dead = nullptr;

// Icon bar
static lv_obj_t* _icon_cell[(int)MenuItem::MENU_COUNT];
static lv_obj_t* _icon_lbl[(int)MenuItem::MENU_COUNT];

// Age / Battery / WiFi (right side of icon bar)
static lv_obj_t* _lbl_age  = nullptr;
static lv_obj_t* _lbl_batt = nullptr;
static lv_obj_t* _lbl_wifi = nullptr;

// Vitals
static lv_obj_t* _bar_hunger  = nullptr;
static lv_obj_t* _bar_happy   = nullptr;
static lv_obj_t* _bar_health  = nullptr;
static lv_obj_t* _lbl_hun_pct = nullptr;
static lv_obj_t* _lbl_hap_pct = nullptr;
static lv_obj_t* _lbl_hlt_pct = nullptr;

// Context hint labels (dynamic A/B/C)
static lv_obj_t* _lbl_hint_a = nullptr;
static lv_obj_t* _lbl_hint_b = nullptr;
static lv_obj_t* _lbl_hint_c = nullptr;

// Overlay panel inside sprite zone
static lv_obj_t* _overlay_panel = nullptr;
#define OVERLAY_LINES 8
static lv_obj_t* _overlay_lbl[OVERLAY_LINES];

// Evolve overlay
static lv_obj_t*  _lbl_evolve   = nullptr;

// P1 incident indicator — shown in sprite zone when hasP1 is true
static lv_obj_t*  _lbl_p1       = nullptr;

// Push indicator — animates the WiFi symbol in the icon bar:
//   phase 1 (immediate): LV_SYMBOL_UP in orange  (upload fired)
//   phase 2 (400 ms):    LV_SYMBOL_UP in green   (success only)
//   phase 3 (2000 ms):   restore LV_SYMBOL_WIFI  (ui_update_header corrects colour)
static lv_timer_t* _wifi_green_timer   = nullptr;
static lv_timer_t* _wifi_restore_timer = nullptr;

static void wifi_green_cb(lv_timer_t* t) {
    lv_timer_del(t);
    _wifi_green_timer = nullptr;
    if (_lbl_wifi)
        lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x3B, 0xD4, 0x79), 0);
}

static void wifi_restore_cb(lv_timer_t* t) {
    lv_timer_del(t);
    _wifi_restore_timer = nullptr;
    // Restore symbol — ui_update_header() will correct the colour on the next tick
    if (_lbl_wifi) {
        lv_label_set_text(_lbl_wifi, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x66, 0x66, 0x77), 0);
    }
}

// =============================================================================
// Styles
// =============================================================================
static lv_style_t _sty_bg;
static lv_style_t _sty_bar_bg;
static lv_style_t _sty_icon_normal;
static lv_style_t _sty_icon_sel;
static lv_style_t _sty_overlay;
static bool _styles_init = false;

static void init_styles() {
    if (_styles_init) return;
    _styles_init = true;

    lv_style_init(&_sty_bg);
    lv_style_set_bg_color(&_sty_bg, COL_BG);
    lv_style_set_border_width(&_sty_bg, 0);
    lv_style_set_pad_all(&_sty_bg, 0);
    lv_style_set_radius(&_sty_bg, 0);

    lv_style_init(&_sty_bar_bg);
    lv_style_set_bg_color(&_sty_bar_bg, lv_color_make(0x33, 0x33, 0x44));
    lv_style_set_border_width(&_sty_bar_bg, 0);
    lv_style_set_radius(&_sty_bar_bg, 4);

    // Unselected icon cell — transparent, no border
    lv_style_init(&_sty_icon_normal);
    lv_style_set_bg_opa(&_sty_icon_normal, LV_OPA_TRANSP);
    lv_style_set_border_width(&_sty_icon_normal, 0);
    lv_style_set_pad_all(&_sty_icon_normal, 2);
    lv_style_set_radius(&_sty_icon_normal, 3);

    // Selected icon cell — orange border highlight
    lv_style_init(&_sty_icon_sel);
    lv_style_set_bg_opa(&_sty_icon_sel, LV_OPA_TRANSP);
    lv_style_set_border_color(&_sty_icon_sel, GRAFANA_ORANGE);
    lv_style_set_border_width(&_sty_icon_sel, 2);
    lv_style_set_pad_all(&_sty_icon_sel, 2);
    lv_style_set_radius(&_sty_icon_sel, 3);

    // Overlay panel — semi-transparent dark card
    lv_style_init(&_sty_overlay);
    lv_style_set_bg_color(&_sty_overlay, lv_color_make(0x0D, 0x0D, 0x16));
    lv_style_set_bg_opa(&_sty_overlay, LV_OPA_90);
    lv_style_set_border_color(&_sty_overlay, GRAFANA_ORANGE);
    lv_style_set_border_width(&_sty_overlay, 1);
    lv_style_set_pad_all(&_sty_overlay, 7);
    lv_style_set_radius(&_sty_overlay, 6);
}

// =============================================================================
// Bar / label helpers
// =============================================================================
static lv_obj_t* make_bar(lv_obj_t* parent, lv_color_t colour, lv_coord_t x, lv_coord_t w) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, BARS_H - 4);
    lv_obj_set_pos(bar, x, 2);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 80, LV_ANIM_OFF);
    lv_obj_add_style(bar, &_sty_bar_bg, LV_PART_MAIN);

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
    lv_obj_add_style(_screen_game, &_sty_bg, 0);

    // -------------------------------------------------------------------------
    // Icon bar (y=0, h=36)
    // Layout: [S][F][C][G][Z][M]  (6 × 26px)  |  age  wifi
    // -------------------------------------------------------------------------
    lv_obj_t* icon_bar = lv_obj_create(_screen_game);
    lv_obj_set_size(icon_bar, LCD_WIDTH, ICON_BAR_H);
    lv_obj_set_pos(icon_bar, 0, 0);
    lv_obj_add_style(icon_bar, &_sty_bg, 0);
    lv_obj_set_style_border_side(icon_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(icon_bar, COL_DIVIDER, 0);
    lv_obj_set_style_border_width(icon_bar, 1, 0);
    lv_obj_clear_flag(icon_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Icon cells: 24×28 each, 2px gap.
    // ICON_PAD_X keeps icons clear of the display's rounded physical corners.
    const lv_coord_t CELL_W    = 24;
    const lv_coord_t CELL_H    = 28;
    const lv_coord_t CELL_GAP  = 2;
    const lv_coord_t ICON_PAD_X = 10;   // left/right inset from rounded corners
    const lv_coord_t ICONS_END = ICON_PAD_X + (int)MenuItem::MENU_COUNT * (CELL_W + CELL_GAP);

    for (int i = 0; i < (int)MenuItem::MENU_COUNT; i++) {
        lv_obj_t* cell = lv_obj_create(icon_bar);
        lv_obj_set_size(cell, CELL_W, CELL_H);
        lv_obj_set_pos(cell, ICON_PAD_X + i * (CELL_W + CELL_GAP), 3);
        lv_obj_add_style(cell, &_sty_icon_normal, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        _icon_cell[i] = cell;

        lv_obj_t* sym = lv_label_create(cell);
        lv_label_set_text(sym, MENU_SYMBOLS[i]);
        lv_obj_set_style_text_font(sym, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sym, COL_ICON_DIM, 0);
        lv_obj_center(sym);
        _icon_lbl[i] = sym;
    }

    // Age label (right of icons)
    _lbl_age = lv_label_create(icon_bar);
    lv_label_set_text(_lbl_age, "0m");
    lv_obj_set_style_text_font(_lbl_age, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_lbl_age, lv_color_make(0x99, 0x99, 0xAA), 0);
    lv_obj_set_pos(_lbl_age, ICONS_END + 2, 10);

    // Battery symbol (just left of WiFi)
    _lbl_batt = lv_label_create(icon_bar);
    lv_label_set_text(_lbl_batt, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_font(_lbl_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_batt, lv_color_make(0x66, 0x66, 0x77), 0);
    lv_obj_set_pos(_lbl_batt, LCD_WIDTH - 44, 8);

    // WiFi symbol (far right)
    _lbl_wifi = lv_label_create(icon_bar);
    lv_label_set_text(_lbl_wifi, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x66, 0x66, 0x77), 0);
    lv_obj_set_pos(_lbl_wifi, LCD_WIDTH - 22, 8);

    // -------------------------------------------------------------------------
    // Sprite zone (y=36, h=150)
    // -------------------------------------------------------------------------
    lv_obj_t* sprite_zone = lv_obj_create(_screen_game);
    lv_obj_set_size(sprite_zone, LCD_WIDTH, SPRITE_H);
    lv_obj_set_pos(sprite_zone, 0, ICON_BAR_H);
    lv_obj_add_style(sprite_zone, &_sty_bg, 0);
    lv_obj_clear_flag(sprite_zone, LV_OBJ_FLAG_SCROLLABLE);

    sprite_engine_init(sprite_zone);

    // Overlay panel — parented to sprite zone, hidden by default
    _overlay_panel = lv_obj_create(sprite_zone);
    lv_obj_set_size(_overlay_panel, LCD_WIDTH - 20, SPRITE_H - 16);
    lv_obj_set_pos(_overlay_panel, 10, 8);
    lv_obj_add_style(_overlay_panel, &_sty_overlay, 0);
    lv_obj_clear_flag(_overlay_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_overlay_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_overlay_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_overlay_panel, 3, 0);

    for (int i = 0; i < OVERLAY_LINES; i++) {
        lv_obj_t* l = lv_label_create(_overlay_panel);
        lv_label_set_text(l, "");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_set_width(l, LV_PCT(100));
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        _overlay_lbl[i] = l;
    }
    lv_obj_add_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);

    // P1 incident indicator — bottom-right of sprite zone, hidden by default
    _lbl_p1 = lv_label_create(sprite_zone);
    lv_label_set_text(_lbl_p1, "  !! P1 !!  ");
    lv_obj_set_style_text_font(_lbl_p1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_p1, lv_color_make(0xFF, 0x20, 0x20), 0);  // alert red
    lv_obj_set_style_bg_color(_lbl_p1, lv_color_make(0x1A, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(_lbl_p1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_lbl_p1, lv_color_make(0xFF, 0x20, 0x20), 0);
    lv_obj_set_style_border_width(_lbl_p1, 1, 0);
    lv_obj_set_style_pad_hor(_lbl_p1, 6, 0);
    lv_obj_set_style_pad_ver(_lbl_p1, 3, 0);
    lv_obj_set_style_radius(_lbl_p1, 4, 0);
    lv_obj_align(_lbl_p1, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_add_flag(_lbl_p1, LV_OBJ_FLAG_HIDDEN);

    // -------------------------------------------------------------------------
    // Vitals bars (y=186, h=22)
    // -------------------------------------------------------------------------
    lv_obj_t* vitals = lv_obj_create(_screen_game);
    lv_obj_set_size(vitals, LCD_WIDTH, BARS_H);
    lv_obj_set_pos(vitals, 0, ICON_BAR_H + SPRITE_H);
    lv_obj_add_style(vitals, &_sty_bg, 0);
    lv_obj_set_style_pad_all(vitals, 0, 0);
    lv_obj_clear_flag(vitals, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t bar_w = (LCD_WIDTH - 6) / 3;
    _bar_hunger = make_bar(vitals, COL_HUNGER, 2,                   bar_w);
    _bar_happy  = make_bar(vitals, COL_HAPPY,  2 + bar_w + 2,       bar_w);
    _bar_health = make_bar(vitals, COL_HEALTH, 2 + (bar_w + 2) * 2, bar_w);

    _lbl_hun_pct = make_pct_label(vitals, 2,                   bar_w);
    _lbl_hap_pct = make_pct_label(vitals, 2 + bar_w + 2,       bar_w);
    _lbl_hlt_pct = make_pct_label(vitals, 2 + (bar_w + 2) * 2, bar_w);

    // -------------------------------------------------------------------------
    // Legend row (y=208, h=16)
    // -------------------------------------------------------------------------
    lv_obj_t* legend = lv_obj_create(_screen_game);
    lv_obj_set_size(legend, LCD_WIDTH, LEGEND_H);
    lv_obj_set_pos(legend, 0, ICON_BAR_H + SPRITE_H + BARS_H);
    lv_obj_add_style(legend, &_sty_bg, 0);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_legend_lbl = [&](const char* text, lv_color_t col) {
        lv_obj_t* l = lv_label_create(legend);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(l, col, 0);
    };
    make_legend_lbl("Hunger",    COL_HUNGER);
    make_legend_lbl("Happiness", COL_HAPPY);
    make_legend_lbl("Health",    COL_HEALTH);

    // -------------------------------------------------------------------------
    // Context hints (y=224, h=56)
    // -------------------------------------------------------------------------
    lv_obj_t* hints = lv_obj_create(_screen_game);
    lv_obj_set_size(hints, LCD_WIDTH, HINTS_H);
    lv_obj_set_pos(hints, 0, ICON_BAR_H + SPRITE_H + BARS_H + LEGEND_H);
    lv_obj_add_style(hints, &_sty_bg, 0);
    lv_obj_set_style_border_side(hints, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(hints, COL_DIVIDER, 0);
    lv_obj_set_style_border_width(hints, 1, 0);
    lv_obj_set_flex_flow(hints, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hints, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_hint_lbl = [&](const char* text) -> lv_obj_t* {
        lv_obj_t* l = lv_label_create(hints);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_make(0x77, 0x77, 0x88), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return l;
    };
    _lbl_hint_a = make_hint_lbl("");
    _lbl_hint_b = make_hint_lbl("");
    _lbl_hint_c = make_hint_lbl("");

    // -------------------------------------------------------------------------
    // Evolve overlay (fullscreen label, hidden)
    // -------------------------------------------------------------------------
    _lbl_evolve = lv_label_create(_screen_game);
    lv_label_set_text(_lbl_evolve, "EVOLVING...");
    lv_obj_set_style_text_font(_lbl_evolve, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_evolve, GRAFANA_ORANGE, 0);
    // White lozenge background with orange border
    lv_obj_set_style_bg_color(_lbl_evolve, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_lbl_evolve, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_lbl_evolve, GRAFANA_ORANGE, 0);
    lv_obj_set_style_border_width(_lbl_evolve, 2, 0);
    lv_obj_set_style_radius(_lbl_evolve, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(_lbl_evolve, 18, 0);
    lv_obj_set_style_pad_ver(_lbl_evolve, 8, 0);
    lv_obj_center(_lbl_evolve);
    lv_obj_add_flag(_lbl_evolve, LV_OBJ_FLAG_HIDDEN);

}

static void build_dead_screen() {
    if (!_styles_init) init_styles();
    _screen_dead = lv_obj_create(nullptr);
    lv_obj_add_style(_screen_dead, &_sty_bg, 0);

    lv_obj_t* lbl = lv_label_create(_screen_dead);
    lv_label_set_text(lbl, "GROT HAS RETURNED\nTO THE DATA STREAM\n\nPress B to restart");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC, 0x33, 0x33), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
}

// =============================================================================
// Public API — screens
// =============================================================================
void ui_screens_init() {
    init_styles();
    build_game_screen();
    build_dead_screen();
}

void ui_show_game() { lv_screen_load(_screen_game); }
void ui_show_dead() { lv_screen_load(_screen_dead); }

// =============================================================================
// Public API — vitals + header
// =============================================================================
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

    // Show/hide P1 incident indicator
    if (_lbl_p1) {
        if (p->hasP1) lv_obj_clear_flag(_lbl_p1, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(_lbl_p1,   LV_OBJ_FLAG_HIDDEN);
    }

    // Flash the CLEAN icon red when P1 is active, dim otherwise
    if (_icon_lbl[(int)MenuItem::CLEAN]) {
        lv_obj_set_style_text_color(_icon_lbl[(int)MenuItem::CLEAN],
            p->hasP1 ? lv_color_make(0xFF, 0x20, 0x20) : COL_ICON_DIM, 0);
    }
}

void ui_update_header(const PetState* p, bool wifi_connected, uint8_t battery_pct) {
    if (!_lbl_age) return;

    char age_buf[16];
    uint32_t mins  = p->ageSeconds / 60;
    uint32_t hours = mins / 60;
    mins %= 60;
    if (hours > 0) snprintf(age_buf, sizeof(age_buf), "%uh%um", hours, mins);
    else           snprintf(age_buf, sizeof(age_buf), "%um", mins);
    lv_label_set_text(_lbl_age, age_buf);

    if (wifi_connected) {
        lv_label_set_text(_lbl_wifi, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x3B, 0xD4, 0x79), 0);
    } else {
        lv_label_set_text(_lbl_wifi, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(_lbl_wifi, lv_color_make(0x66, 0x66, 0x77), 0);
    }

    if (_lbl_batt) {
        const char* sym;
        lv_color_t  col;
        if      (battery_pct >= 80) { sym = LV_SYMBOL_BATTERY_FULL;  col = lv_color_make(0x3B, 0xD4, 0x79); }
        else if (battery_pct >= 60) { sym = LV_SYMBOL_BATTERY_3;     col = lv_color_make(0x3B, 0xD4, 0x79); }
        else if (battery_pct >= 40) { sym = LV_SYMBOL_BATTERY_2;     col = lv_color_make(0xFF, 0xCC, 0x44); }
        else if (battery_pct >= 20) { sym = LV_SYMBOL_BATTERY_1;     col = lv_color_make(0xFF, 0x7F, 0x00); }
        else                        { sym = LV_SYMBOL_BATTERY_EMPTY; col = lv_color_make(0xFF, 0x20, 0x20); }
        lv_label_set_text(_lbl_batt, sym);
        lv_obj_set_style_text_color(_lbl_batt, col, 0);
    }
}

// =============================================================================
// Public API — icon menu
// =============================================================================
void ui_menu_set_selected(MenuItem item) {
    for (int i = 0; i < (int)MenuItem::MENU_COUNT; i++) {
        if (!_icon_cell[i]) continue;
        bool sel = (item != MenuItem::MENU_COUNT) && (i == (int)item);
        lv_obj_remove_style(_icon_cell[i], sel ? &_sty_icon_normal : &_sty_icon_sel, 0);
        lv_obj_add_style(_icon_cell[i], sel ? &_sty_icon_sel : &_sty_icon_normal, 0);
        lv_obj_set_style_text_color(_icon_lbl[i],
            sel ? GRAFANA_ORANGE : COL_ICON_DIM, 0);
        lv_obj_invalidate(_icon_cell[i]);
    }
}

// =============================================================================
// Public API — context hints
// =============================================================================
void ui_set_hints(const char* a, const char* b, const char* c) {
    if (_lbl_hint_a) lv_label_set_text(_lbl_hint_a, a ? a : "");
    if (_lbl_hint_b) lv_label_set_text(_lbl_hint_b, b ? b : "");
    if (_lbl_hint_c) lv_label_set_text(_lbl_hint_c, c ? c : "");
}

// =============================================================================
// Overlay helpers (internal)
// =============================================================================
static void overlay_clear_lines() {
    for (int i = 0; i < OVERLAY_LINES; i++) {
        if (_overlay_lbl[i]) {
            lv_label_set_text(_overlay_lbl[i], "");
            lv_obj_add_flag(_overlay_lbl[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void overlay_set_line(int idx, const char* text,
                             const lv_font_t* font = &lv_font_montserrat_12,
                             lv_color_t col = lv_color_white()) {
    if (idx < 0 || idx >= OVERLAY_LINES || !_overlay_lbl[idx]) return;
    lv_label_set_text(_overlay_lbl[idx], text);
    lv_obj_set_style_text_font(_overlay_lbl[idx], font, 0);
    lv_obj_set_style_text_color(_overlay_lbl[idx], col, 0);
    lv_obj_clear_flag(_overlay_lbl[idx], LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Public API — overlays
// =============================================================================
void ui_show_overlay_status(const PetState* p, float battery_v, const char* name) {
    overlay_clear_lines();

    char buf[40];
    // Line 0: Grot's unique name
    if (name && name[0]) {
        snprintf(buf, sizeof(buf), "-- %s --", name);
        overlay_set_line(0, buf, &lv_font_montserrat_12, GRAFANA_ORANGE);
    } else {
        overlay_set_line(0, "-- Status --", &lv_font_montserrat_12, GRAFANA_ORANGE);
    }

    snprintf(buf, sizeof(buf), "Hunger:    %d%%", p->hunger);
    overlay_set_line(1, buf, &lv_font_montserrat_12, COL_HUNGER);

    snprintf(buf, sizeof(buf), "Happiness: %d%%", p->happiness);
    overlay_set_line(2, buf, &lv_font_montserrat_12, COL_HAPPY);

    snprintf(buf, sizeof(buf), "Health:    %d%%", p->health);
    overlay_set_line(3, buf, &lv_font_montserrat_12, COL_HEALTH);

    uint32_t mins  = p->ageSeconds / 60;
    uint32_t hours = mins / 60;
    mins %= 60;
    if (hours > 0) snprintf(buf, sizeof(buf), "Age:       %luh %lum", hours, mins);
    else           snprintf(buf, sizeof(buf), "Age:       %lum", mins);
    overlay_set_line(4, buf);

    snprintf(buf, sizeof(buf), "Mistakes:  %d", p->careMistakes);
    overlay_set_line(5, buf, &lv_font_montserrat_12,
                    p->careMistakes > 3 ? lv_color_make(0xFF, 0x40, 0x40)
                                       : lv_color_make(0xFF, 0xCC, 0x44));

    snprintf(buf, sizeof(buf), "Battery:   %.2fV", battery_v);
    overlay_set_line(6, buf, &lv_font_montserrat_12, lv_color_make(0xAA, 0xAA, 0xBB));

    if (p->hasP1)
        overlay_set_line(7, "!! P1 Firing !!", &lv_font_montserrat_12,
                         lv_color_make(0xFF, 0x20, 0x20));

    if (_overlay_panel) lv_obj_clear_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_overlay_feed(int choice) {
    // choice: 0 = Microchip, 1 = SIN-wave
    overlay_clear_lines();

    overlay_set_line(0, "-- Feed Grot --", &lv_font_montserrat_12, GRAFANA_ORANGE);

    overlay_set_line(2, choice == 0 ? "> Microchip " : "  Microchip ",
                     &lv_font_montserrat_12,
                     choice == 0 ? GRAFANA_ORANGE : lv_color_make(0x88, 0x88, 0x99));
    overlay_set_line(3, "  +20 Hunger  +5 Happy",
                     &lv_font_montserrat_10, lv_color_make(0x77, 0x77, 0x88));

    overlay_set_line(5, choice == 1 ? "> SIN-wave  " : "  SIN-wave  ",
                     &lv_font_montserrat_12,
                     choice == 1 ? GRAFANA_ORANGE : lv_color_make(0x88, 0x88, 0x99));
    overlay_set_line(6, "  +10 Hunger +15 Happy",
                     &lv_font_montserrat_10, lv_color_make(0x77, 0x77, 0x88));

    if (_overlay_panel) lv_obj_clear_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_overlay_text(const char* msg) {
    overlay_clear_lines();
    overlay_set_line(0, msg ? msg : "");
    if (_overlay_panel) lv_obj_clear_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);
}

void ui_hide_overlay() {
    if (_overlay_panel) lv_obj_add_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Public API — evolve overlay + push toast
// =============================================================================
void ui_show_evolve_overlay(bool visible) {
    if (!_lbl_evolve) return;
    if (visible) lv_obj_clear_flag(_lbl_evolve, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(_lbl_evolve,   LV_OBJ_FLAG_HIDDEN);
}

void ui_show_push_toast(bool success) {
    if (!_lbl_wifi) return;

    // Cancel any in-flight animation timers
    if (_wifi_green_timer)   { lv_timer_del(_wifi_green_timer);   _wifi_green_timer   = nullptr; }
    if (_wifi_restore_timer) { lv_timer_del(_wifi_restore_timer); _wifi_restore_timer = nullptr; }

    // Phase 1: orange UP arrow (upload occurred)
    lv_label_set_text(_lbl_wifi, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(_lbl_wifi, GRAFANA_ORANGE, 0);

    if (success) {
        // Phase 2: switch to green after 400 ms
        _wifi_green_timer = lv_timer_create(wifi_green_cb, 400, nullptr);
        lv_timer_set_repeat_count(_wifi_green_timer, 1);
    }

    // Phase 3: restore wifi symbol after 2 s
    _wifi_restore_timer = lv_timer_create(wifi_restore_cb, 2000, nullptr);
    lv_timer_set_repeat_count(_wifi_restore_timer, 1);
}
