#pragma once
#include <lvgl.h>
#include "../game/pet_state.h"

// =============================================================================
// Sprite Engine — manages the animated LVGL image object for Grot
// =============================================================================

// Call once in setup(), passing the parent LVGL object (e.g. lv_screen_active())
void sprite_engine_init(lv_obj_t* parent);

// Call when pet stage/emotion/quality changes.
// The engine selects the correct frame table and restarts animation.
void sprite_engine_set_state(LifeStage stage, GrotEmotion emotion, EvoQuality quality);

// Returns the lv_obj_t* of the sprite image (for repositioning if needed)
lv_obj_t* sprite_engine_get_obj();

// Cycle through animation states for development testing.
// Each call advances to the next state in the sequence and prints it to Serial.
void sprite_engine_test_next();
