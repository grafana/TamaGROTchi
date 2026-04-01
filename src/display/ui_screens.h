#pragma once
#include <lvgl.h>
#include "../game/pet_state.h"

// =============================================================================
// UI Screens — LVGL-based game interface
//
// Layout (240×280):
//   Icon bar  36px  — 6 icons + age/wifi (A=prev, B=select, C=next)
//   Sprite   150px  — Grot animation zone
//   Bars      22px  — hunger/happiness/health
//   Legend    16px  — labels under bars
//   Hints     56px  — context-sensitive A/B/C button labels
// =============================================================================

// Icon menu items
enum class MenuItem : uint8_t {
    STATUS   = 0,  // LV_SYMBOL_LIST   — full stats overlay
    FEED     = 1,  // LV_SYMBOL_PLUS   — food choice sub-menu
    CLEAN    = 2,  // LV_SYMBOL_TRASH  — clear waste
    GAME     = 3,  // LV_SYMBOL_PLAY   — mini-game (stub)
    LIGHTS   = 4,  // LV_SYMBOL_POWER  — toggle sleep/wake
    MEDICINE = 5,  // LV_SYMBOL_CHARGE — give medicine
    MENU_COUNT = 6,
};

// Initialise all screens once in setup() after lvgl_port_init()
void ui_screens_init();

// Switch the active screen
void ui_show_game();
void ui_show_dead();

// Update dynamic widgets each loop (after game_engine_update)
void ui_update_vitals(const PetState* p);
void ui_update_header(const PetState* p, bool wifi_connected);

// Icon menu — highlight the given icon (MENU_COUNT = clear all)
void ui_menu_set_selected(MenuItem item);

// Overlays rendered inside the sprite zone
void ui_show_overlay_status(const PetState* p, float battery_v, const char* name);
void ui_show_overlay_feed(int choice);      // choice: 0=Microchip, 1=SIN-wave
void ui_show_overlay_text(const char* msg); // generic message (Clean result, Game stub, etc.)
void ui_hide_overlay();

// Context hints bar — pass "" to blank a slot
void ui_set_hints(const char* a, const char* b, const char* c);

// Evolve overlay and push toast (unchanged)
void ui_show_evolve_overlay(bool visible);
void ui_show_push_toast(bool success);
