#pragma once
#include <lvgl.h>
#include "../game/pet_state.h"

// =============================================================================
// UI Screens — LVGL-based game interface
// =============================================================================

// Initialise all screens once in setup() after lvgl_port_init()
void ui_screens_init();

// Switch the active screen
void ui_show_game();          // Main gameplay screen
void ui_show_dead();          // Death screen with restart prompt

// Update dynamic widgets each loop (after game_engine_update)
void ui_update_vitals(const PetState* p);
void ui_update_header(const PetState* p, bool wifi_connected);

// Brief toast for Grafana push result (auto-hides after 2 s)
void ui_show_push_toast(bool success);

// Show "evolving..." overlay
void ui_show_evolve_overlay(bool visible);
