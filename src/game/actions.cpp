#include "actions.h"
#include "../config.h"
#include "../buzzer/buzzer.h"
#include <Arduino.h>

extern void game_log(uint8_t level, const char* event, const char* msg);

static bool is_interactive(const PetState* p) {
    return p->status != PetStatus::DEAD &&
           p->status != PetStatus::EVOLVING;
}

static uint8_t clamp(int v) {
    if (v < 0)   return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

bool action_feed(PetState* p, FoodType food) {
    if (!is_interactive(p)) return false;

    uint8_t hun_before = p->hunger;
    uint8_t hap_before = p->happiness;

    if (food == FoodType::MICROCHIP) {
        p->hunger    = clamp((int)p->hunger    + 20);
        p->happiness = clamp((int)p->happiness +  5);
    } else {  // SIN_WAVE
        p->hunger    = clamp((int)p->hunger    + 10);
        p->happiness = clamp((int)p->happiness + 15);
    }

    p->status        = PetStatus::EATING;
    p->statusStartMs = millis();
    p->hungerAlertSent = false;

    const char* food_name = (food == FoodType::MICROCHIP) ? "microchip" : "sin_wave";
    char msg[80];
    snprintf(msg, sizeof(msg),
             "food=%s | hunger=%d->%d | happiness=%d->%d",
             food_name, hun_before, p->hunger, hap_before, p->happiness);
    game_log(9 /*INFO*/, "fed", msg);

    buzzer_play_async(MELODY_FEED, MELODY_FEED_LEN);
    return true;
}

bool action_play(PetState* p) {
    if (!is_interactive(p)) return false;

    uint8_t before = p->happiness;
    p->happiness = clamp((int)p->happiness + 20);
    p->status    = PetStatus::PLAYING;
    p->statusStartMs = millis();

    // Clear happiness alert
    p->happinessAlertSent = false;

    char msg[60];
    snprintf(msg, sizeof(msg), "happiness_before=%d | happiness_after=%d", before, p->happiness);
    game_log(9 /*INFO*/, "played", msg);

    buzzer_play_async(MELODY_HAPPY, MELODY_HAPPY_LEN);
    return true;
}

bool action_medicine(PetState* p) {
    if (!is_interactive(p)) return false;

    uint8_t before = p->health;
    bool was_sick  = (p->status == PetStatus::SICK);
    p->health = clamp((int)p->health + 30);
    if (was_sick) p->status = PetStatus::IDLE;

    char msg[80];
    snprintf(msg, sizeof(msg), "health_before=%d | health_after=%d | sick_cleared=%s",
             before, p->health, was_sick ? "true" : "false");
    game_log(9 /*INFO*/, "medicine", msg);

    if (was_sick) {
        char rec_msg[40];
        snprintf(rec_msg, sizeof(rec_msg), "health=%d", p->health);
        game_log(9 /*INFO*/, "recovered", rec_msg);
    }

    buzzer_play_async(MELODY_MEDICINE, MELODY_MEDICINE_LEN);
    return true;
}

bool action_discipline(PetState* p) {
    if (!is_interactive(p)) return false;

    uint8_t before = p->disciplineScore;
    p->disciplineScore = clamp((int)p->disciplineScore + 5);

    char msg[60];
    snprintf(msg, sizeof(msg), "discipline_before=%d | discipline_after=%d",
             before, p->disciplineScore);
    game_log(9 /*INFO*/, "discipline", msg);
    return true;
}

void action_dizzy(PetState* p, float accel_mag) {
    if (p->status == PetStatus::DEAD) return;

    p->happiness = clamp((int)p->happiness - 5);
    p->status    = PetStatus::DIZZY;
    p->statusStartMs = millis();

    char msg[60];
    snprintf(msg, sizeof(msg), "accel_mag=%.1f | happiness_lost=5", (double)accel_mag);
    game_log(9 /*INFO*/, "shake_dizzy", msg);

    buzzer_play_async(MELODY_DIZZY, MELODY_DIZZY_LEN);
}

void action_wake(PetState* p, uint8_t current_hour) {
    p->isSleeping = false;
    p->status     = PetStatus::IDLE;
    p->statusStartMs = millis();

    bool early = (current_hour < p->wakeHour);
    if (early) p->careMistakes++;

    char msg[60];
    snprintf(msg, sizeof(msg), "hour=%d | care_mistakes=%d", current_hour, p->careMistakes);
    game_log(early ? 13 : 9, "woken_by_shake", msg);
}
