#include "evolution.h"
#include "../config.h"
#include "../buzzer/buzzer.h"
#include <Arduino.h>

// Forward declaration (defined weakly in game_engine.cpp)
extern void game_log(uint8_t level, const char* event, const char* msg);

// Age thresholds (seconds) — configured in config.h, demo-speed by default
static const uint32_t THRESHOLDS[] = {
    EVO_EGG_TO_BABY_S,
    EVO_BABY_TO_CHILD_S,
    EVO_CHILD_TO_TEEN_S,
    EVO_TEEN_TO_ADULT_S,
    EVO_ADULT_TO_SENIOR_S,
    EVO_SENIOR_LIFE_S,
};

static EvoQuality compute_quality(uint8_t mistakes) {
    if (mistakes <= EVO_QUALITY_EXCELLENT) return EvoQuality::EXCELLENT;
    if (mistakes <= EVO_QUALITY_GOOD)      return EvoQuality::GOOD;
    return EvoQuality::TIRED;
}

void evolution_check(PetState* p) {
    if (p->evolveReady) return;   // already pending
    if (p->status == PetStatus::DEAD || p->status == PetStatus::EVOLVING) return;

    uint8_t stage_idx = static_cast<uint8_t>(p->stage);
    if (stage_idx >= 5) return;  // SENIOR or DEAD — no more evolution

    if (p->ageSeconds >= THRESHOLDS[stage_idx]) {
        p->evolveReady = true;
    }
}

void evolution_advance(PetState* p) {
    if (!p->evolveReady) return;
    p->evolveReady = false;

    LifeStage prev = p->stage;
    uint8_t next_idx = static_cast<uint8_t>(p->stage) + 1;
    if (next_idx > static_cast<uint8_t>(LifeStage::SENIOR)) {
        // Senior's time is up → death (natural causes)
        p->status = PetStatus::DEAD;
        p->stage  = LifeStage::DEAD;

        char msg[80];
        snprintf(msg, sizeof(msg), "age_s=%u | care_mistakes=%d | cause=old_age", p->ageSeconds, p->careMistakes);
        game_log(17 /*ERROR*/, "death", msg);
        buzzer_play_async(MELODY_DEAD, MELODY_DEAD_LEN);
        return;
    }

    p->stage   = static_cast<LifeStage>(next_idx);
    p->quality = compute_quality(p->careMistakes);
    p->status  = PetStatus::IDLE;

    static const char* quality_names[] = {"excellent", "good", "tired"};

    char msg[120];
    snprintf(msg, sizeof(msg), "from=%s | to=%s | age_s=%u | care_mistakes=%d | quality=%s",
             life_stage_name(prev),
             life_stage_name(p->stage),
             p->ageSeconds,
             p->careMistakes,
             quality_names[static_cast<uint8_t>(p->quality)]);
    game_log(9 /*INFO*/, "evolved", msg);

    buzzer_play_async(MELODY_EVOLVE, MELODY_EVOLVE_LEN);
}
