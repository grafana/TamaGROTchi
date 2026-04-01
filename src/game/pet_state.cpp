#include "pet_state.h"
#include "../config.h"
#include <Arduino.h>

void pet_state_init(PetState* p) {
    p->hunger          = 80;
    p->happiness       = 80;
    p->health          = 100;

    p->stage           = LifeStage::EGG;
    p->ageSeconds      = 0;
    p->lastTickMs      = millis();

    p->careMistakes    = 0;
    p->disciplineScore = 50;
    p->evolveReady     = false;
    p->quality         = EvoQuality::EXCELLENT;

    p->status          = PetStatus::IDLE;
    p->statusStartMs   = millis();

    p->isSleeping      = false;
    p->sleepHour       = 22;
    p->wakeHour        = 8;

    p->sickSinceMs        = 0;
    p->lowHungerStartMs   = 0;

    p->alertActive        = false;
    p->alertStartMs       = 0;
    p->hungerAlertSent    = false;
    p->happinessAlertSent = false;

    p->hasP1              = false;
    p->nextP1S            = P1_SPAWN_INTERVAL_S;
    p->p1AlertSent        = false;
}

const char* life_stage_name(LifeStage s) {
    switch (s) {
        case LifeStage::EGG:    return "egg";
        case LifeStage::BABY:   return "baby";
        case LifeStage::CHILD:  return "child";
        case LifeStage::TEEN:   return "teen";
        case LifeStage::ADULT:  return "adult";
        case LifeStage::SENIOR: return "senior";
        case LifeStage::DEAD:   return "dead";
        default:                return "unknown";
    }
}

const char* pet_status_name(PetStatus s) {
    switch (s) {
        case PetStatus::IDLE:     return "idle";
        case PetStatus::EATING:   return "eating";
        case PetStatus::PLAYING:  return "playing";
        case PetStatus::SLEEPING: return "sleeping";
        case PetStatus::SICK:     return "sick";
        case PetStatus::EVOLVING: return "evolving";
        case PetStatus::DEAD:     return "dead";
        case PetStatus::ALERT:    return "alert";
        case PetStatus::DIZZY:    return "dizzy";
        default:                  return "unknown";
    }
}
