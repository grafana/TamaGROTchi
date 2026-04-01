#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Tamagrotchi Pet State — shared by all game modules
// =============================================================================

enum class LifeStage : uint8_t {
    EGG     = 0,
    BABY    = 1,
    CHILD   = 2,
    TEEN    = 3,
    ADULT   = 4,
    SENIOR  = 5,
    DEAD    = 6,
};

enum class PetStatus : uint8_t {
    IDLE     = 0,
    EATING   = 1,
    PLAYING  = 2,
    SLEEPING = 3,
    SICK     = 4,
    EVOLVING = 5,
    DEAD     = 6,
    ALERT    = 7,   // needs attention
    DIZZY    = 8,   // post hard-shake
};

enum class GrotEmotion : uint8_t {
    NORMAL   = 0,
    HAPPY    = 1,
    SAD      = 2,
    SICK     = 3,
    SLEEPING = 4,
    EVOLVING = 5,
    DEAD     = 6,
    DIZZY    = 7,
};

// Evolution visual quality (set at each evolution based on care mistakes)
enum class EvoQuality : uint8_t {
    EXCELLENT = 0,  // 0-1 mistakes: premium Grot
    GOOD      = 1,  // 2-3 mistakes: standard Grot
    TIRED     = 2,  // 4+ mistakes: scruffy Grot
};

struct PetState {
    // ---- Vitals (0-100) ----
    uint8_t   hunger;       // 0=starving, 100=full
    uint8_t   happiness;    // 0=miserable, 100=ecstatic
    uint8_t   health;       // 0=dying, 100=perfect

    // ---- Lifecycle ----
    LifeStage stage;
    uint32_t  ageSeconds;   // total seconds since birth
    uint32_t  lastTickMs;   // millis() at last game tick

    // ---- Evolution ----
    uint8_t   careMistakes;
    uint8_t   disciplineScore;  // 0-100
    bool      evolveReady;      // set by evolution_check(), cleared after animation
    EvoQuality quality;         // locked in at each evolution

    // ---- Status machine ----
    PetStatus status;
    uint32_t  statusStartMs;   // when current status began

    // ---- Sleeping ----
    bool      isSleeping;
    uint8_t   sleepHour;    // hour pet goes to sleep (default 22)
    uint8_t   wakeHour;     // hour pet wakes (default 8)

    // ---- Sickness ----
    uint32_t  sickSinceMs;          // when pet became sick
    uint32_t  lowHungerStartMs;     // for sustained-low-hunger → sick tracking

    // ---- Alerts ----
    bool      alertActive;
    uint32_t  alertStartMs;
    bool      hungerAlertSent;
    bool      happinessAlertSent;

    // ---- P1 incident ----
    bool      hasP1;            // P1 incident is active
    uint32_t  nextP1S;          // ageSeconds when next P1 spawns
    bool      p1AlertSent;      // log event fired for this P1
};

// Seed a fresh pet state (newborn egg)
void pet_state_init(PetState* p);

// Human-readable stage name
const char* life_stage_name(LifeStage s);

// Human-readable status name
const char* pet_status_name(PetStatus s);
