#pragma once
#include "pet_state.h"
#include <stdint.h>

// =============================================================================
// Game Engine — drives the 1-second tick loop, stat decay, and state machine
// =============================================================================

// Call once in setup() after pet_state_init()
void game_engine_init(PetState* state);

// Call every loop() — internally rate-limited to GAME_TICK_MS
// Returns true on the tick frames where a log/metric snapshot is appropriate
bool game_engine_update(PetState* state);

// Derive the display emotion from the current status
GrotEmotion game_engine_get_emotion(const PetState* p);

// Quick state predicates
bool pet_needs_food(const PetState* p);
bool pet_needs_play(const PetState* p);
bool pet_is_sick(const PetState* p);
bool pet_is_dead(const PetState* p);
