#include "game_engine.h"
#include "evolution.h"
#include "../config.h"
#include "../buzzer/buzzer.h"
#include <Arduino.h>
#include <time.h>

// Forward declaration for log emission (defined in main.cpp via otlp_writer)
// We use a weak function so the engine compiles standalone too.
__attribute__((weak)) void game_log(uint8_t level, const char* event, const char* msg) {
    Serial.printf("[%s] %s\n", event, msg);
}

void game_engine_init(PetState* state) {
    state->lastTickMs = millis();
}

// =============================================================================
// Decay helpers
// =============================================================================
static uint8_t clamp_stat(int v) {
    if (v < 0)   return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

static void decay_stats(PetState* p) {
    if (p->stage == LifeStage::EGG) return;  // egg doesn't decay

    // Hunger & happiness drain every tick
    p->hunger    = clamp_stat((int)p->hunger    - HUNGER_DECAY_PER_TICK);
    p->happiness = clamp_stat((int)p->happiness - HAPPINESS_DECAY_PER_TICK);

    // Health slowly recovers if vitals are good
    if (p->hunger > 50 && p->happiness > 50 && p->health < 100) {
        p->health = clamp_stat((int)p->health + 1);
    }
}

static void check_alerts(PetState* p, uint32_t now) {
    bool needs_food = (p->hunger < ALERT_HUNGER_THRESH);
    bool needs_play = (p->happiness < ALERT_HAPPY_THRESH);

    if (needs_food || needs_play) {
        if (!p->alertActive) {
            p->alertActive  = true;
            p->alertStartMs = now;
        }

        // Log hunger alert once
        if (needs_food && !p->hungerAlertSent) {
            p->hungerAlertSent = true;
            char msg[80];
            snprintf(msg, sizeof(msg), "hunger=%d | care_mistakes=%d", p->hunger, p->careMistakes);
            game_log(13 /*WARN*/, "hunger_alert", msg);
            buzzer_play_async(MELODY_ALERT, MELODY_ALERT_LEN);
        }

        // Log happiness alert once
        if (needs_play && !p->happinessAlertSent) {
            p->happinessAlertSent = true;
            char msg[80];
            snprintf(msg, sizeof(msg), "happiness=%d | care_mistakes=%d", p->happiness, p->careMistakes);
            game_log(13 /*WARN*/, "happiness_alert", msg);
        }

        // Check if alert has been ignored too long
        if ((now - p->alertStartMs) >= ALERT_TIMEOUT_MS) {
            p->careMistakes++;
            p->alertActive    = false;
            p->alertStartMs   = 0;
            p->hungerAlertSent    = false;
            p->happinessAlertSent = false;

            char msg[80];
            snprintf(msg, sizeof(msg), "vital=%s | care_mistakes=%d | value=%d",
                     needs_food ? "hunger" : "happiness",
                     p->careMistakes,
                     needs_food ? p->hunger : p->happiness);
            game_log(13 /*WARN*/, "alert_missed", msg);
        }

        if (p->status == PetStatus::IDLE) p->status = PetStatus::ALERT;
    } else {
        // Vitals recovered
        p->alertActive        = false;
        p->hungerAlertSent    = false;
        p->happinessAlertSent = false;
        if (p->status == PetStatus::ALERT) p->status = PetStatus::IDLE;
    }
}

static void check_sickness(PetState* p, uint32_t now) {
    if (p->status == PetStatus::SICK || p->status == PetStatus::DEAD) return;

    // Track sustained low hunger
    if (p->hunger < SICK_HUNGER_THRESH) {
        if (p->lowHungerStartMs == 0) p->lowHungerStartMs = now;
        // Become sick after 60 ticks of low hunger (1 minute)
        if ((now - p->lowHungerStartMs) >= 60000UL) {
            p->status = PetStatus::SICK;
            p->sickSinceMs = now;
            p->lowHungerStartMs = 0;

            char msg[80];
            snprintf(msg, sizeof(msg), "health=%d | hunger_was=%d | happiness_was=%d",
                     p->health, p->hunger, p->happiness);
            game_log(13 /*WARN*/, "sick", msg);
            buzzer_play_async(MELODY_SICK, MELODY_SICK_LEN);
        }
    } else {
        p->lowHungerStartMs = 0;
    }
}

static void drain_sick_health(PetState* p) {
    if (p->status != PetStatus::SICK) return;
    p->health = clamp_stat((int)p->health - SICK_HEALTH_DECAY);
}

static void check_death(PetState* p) {
    if (p->health == 0 && p->status != PetStatus::DEAD) {
        p->status = PetStatus::DEAD;
        p->stage  = LifeStage::DEAD;

        char msg[100];
        snprintf(msg, sizeof(msg), "age_s=%lu | care_mistakes=%d | cause=health_depleted",
                 p->ageSeconds, p->careMistakes);
        game_log(17 /*ERROR*/, "death", msg);
        buzzer_play_async(MELODY_DEAD, MELODY_DEAD_LEN);
    }
}

static void check_sleep(PetState* p) {
    // Only check sleep if RTC / system time is available
    time_t t = time(nullptr);
    if (t < 1000000000L) return;  // time not set yet

    struct tm* ti = localtime(&t);
    uint8_t h = ti->tm_hour;

    if (!p->isSleeping) {
        if (h == p->sleepHour) {
            p->isSleeping = true;
            p->status     = PetStatus::SLEEPING;
            char msg[40];
            snprintf(msg, sizeof(msg), "hour=%d", h);
            game_log(9 /*INFO*/, "sleeping", msg);
        }
    } else {
        if (h == p->wakeHour) {
            p->isSleeping = false;
            p->status     = PetStatus::IDLE;
            char msg[60];
            snprintf(msg, sizeof(msg), "hour=%d | hunger=%d | health=%d",
                     h, p->hunger, p->health);
            game_log(9 /*INFO*/, "woke_up", msg);
        }
        // While sleeping: halved decay handled by skipping decay above
        // Health slowly recovers
        p->health = clamp_stat((int)p->health + 1);
    }
}

static void check_dizzy_timeout(PetState* p, uint32_t now) {
    if (p->status == PetStatus::DIZZY &&
        (now - p->statusStartMs) >= DIZZY_DURATION_MS) {
        p->status = PetStatus::IDLE;
    }
}

// =============================================================================
// Main tick
// =============================================================================
bool game_engine_update(PetState* state) {
    uint32_t now = millis();
    if ((now - state->lastTickMs) < GAME_TICK_MS) return false;
    state->lastTickMs = now;
    state->ageSeconds++;

    // Do nothing while dead
    if (state->status == PetStatus::DEAD) return true;

    // Timed state exits
    check_dizzy_timeout(state, now);

    // Skip decay while sleeping or in action animations
    bool active = (state->status == PetStatus::IDLE ||
                   state->status == PetStatus::ALERT);
    if (active) {
        decay_stats(state);
    } else if (state->status == PetStatus::SLEEPING) {
        // Halved decay while sleeping
        if (state->ageSeconds % 2 == 0) {
            state->hunger    = clamp_stat((int)state->hunger    - HUNGER_DECAY_PER_TICK);
            state->happiness = clamp_stat((int)state->happiness - HAPPINESS_DECAY_PER_TICK);
        }
        state->health = clamp_stat((int)state->health + 1);
    }

    // Drain health when sick
    if (state->status == PetStatus::SICK) drain_sick_health(state);

    // State checks (only when not in a special status)
    if (state->status != PetStatus::EVOLVING &&
        state->status != PetStatus::DEAD &&
        state->status != PetStatus::DIZZY) {
        check_sickness(state, now);
        check_death(state);
        if (state->status != PetStatus::DEAD) {
            check_alerts(state, now);
            check_sleep(state);
        }
    }

    // Evolution check
    evolution_check(state);

    return true;
}

GrotEmotion game_engine_get_emotion(const PetState* p) {
    switch (p->status) {
        case PetStatus::DEAD:     return GrotEmotion::DEAD;
        case PetStatus::SICK:     return GrotEmotion::SICK;
        case PetStatus::SLEEPING: return GrotEmotion::SLEEPING;
        case PetStatus::EVOLVING: return GrotEmotion::EVOLVING;
        case PetStatus::DIZZY:    return GrotEmotion::DIZZY;
        case PetStatus::PLAYING:  return GrotEmotion::HAPPY;
        case PetStatus::EATING:   return GrotEmotion::HAPPY;
        case PetStatus::ALERT:    return GrotEmotion::SAD;
        default:
            if (p->happiness < 30) return GrotEmotion::SAD;
            if (p->happiness > 70) return GrotEmotion::HAPPY;
            return GrotEmotion::NORMAL;
    }
}

bool pet_needs_food(const PetState* p)  { return p->hunger < ALERT_HUNGER_THRESH; }
bool pet_needs_play(const PetState* p)  { return p->happiness < ALERT_HAPPY_THRESH; }
bool pet_is_sick(const PetState* p)     { return p->status == PetStatus::SICK; }
bool pet_is_dead(const PetState* p)     { return p->status == PetStatus::DEAD; }
