#pragma once
#include "pet_state.h"

// =============================================================================
// Player-triggered actions — called from button/shake handler in main.cpp
// =============================================================================

enum class FoodType : uint8_t {
    MICROCHIP,  // hunger +20, happiness +5  — functional food
    SIN_WAVE,   // hunger +10, happiness +15 — fun/party food
};

// Feed Grot with the chosen food type, plays MELODY_FEED
// Returns false if action was blocked (pet dead/evolving)
bool action_feed(PetState* p, FoodType food = FoodType::MICROCHIP);

// Play with Grot: +20 happiness (capped 100), plays MELODY_HAPPY
bool action_play(PetState* p);

// Give medicine: clears sick status, +30 health, plays MELODY_MEDICINE
bool action_medicine(PetState* p);

// Discipline: +5 discipline score
bool action_discipline(PetState* p);

// Apply dizzy effect from a hard shake
void action_dizzy(PetState* p, float accel_mag);

// Wake pet from sleep (e.g. from gentle shake)
// counts as care mistake if before wakeHour
void action_wake(PetState* p, uint8_t current_hour);

// Resolve the P1 incident: clears hasP1, +10 health, schedules next spawn.
// Returns false if there is no active P1 or the pet is dead/evolving.
bool action_clean(PetState* p);
