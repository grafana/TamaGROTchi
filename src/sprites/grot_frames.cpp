#include "grot_frames.h"
#include "../game/pet_state.h"

// =============================================================================
// Frame tables
//
// Sprite → state mapping:
//   grot_0..3               idle bob (4 fr)   → egg idle, baby idle, tired adult idle
//   grot_blink_0..1         blink   (2 fr)    → sleeping
//   grot_wave_0..5          wave    (6 fr)    → happy (baby / child / good adult)
//   grot_walk_0..7          walk R  (8 fr)    → child/adult idle, facing right
//   grot_walk_left_0..7     walk L  (8 fr)    → child/adult idle, facing left
//   grot_jumping_0..7       jump    (8 fr)    → excited adult happy, dizzy (last 4 fr)
//   grot_thinks_0..4        thinks  (5 fr)    → sad, sick, dead
// =============================================================================

// --- Egg ---
static const lv_image_dsc_t* egg_idle[] = {
    &egg_shake_0, &egg_shake_1, &egg_shake_2, nullptr
};

// --- Baby ---
static const lv_image_dsc_t* baby_idle[] = {
    &grot_0, &grot_1, &grot_2, &grot_3, nullptr
};
static const lv_image_dsc_t* baby_happy[] = {
    &grot_wave_0, &grot_wave_1, &grot_wave_2,
    &grot_wave_3, &grot_wave_4, &grot_wave_5, nullptr
};
static const lv_image_dsc_t* baby_sad[] = {
    &grot_thinks_0, &grot_thinks_1, nullptr
};

// --- Child / Teen ---
static const lv_image_dsc_t* child_idle[] = {
    &grot_walk_0, &grot_walk_1, &grot_walk_2, &grot_walk_3, nullptr
};
static const lv_image_dsc_t* child_happy[] = {
    &grot_jumping_0, &grot_jumping_1, &grot_jumping_2, &grot_jumping_3, nullptr
};
static const lv_image_dsc_t* child_sad[] = {
    &grot_thinks_0, &grot_thinks_1, &grot_thinks_2, nullptr
};
static const lv_image_dsc_t* child_sick[] = {
    &grot_thinks_3, nullptr
};

// --- Adult / Senior — Excellent quality ---
static const lv_image_dsc_t* adult_exc_idle[] = {
    &grot_walk_0, &grot_walk_1, &grot_walk_2, &grot_walk_3,
    &grot_walk_4, &grot_walk_5, &grot_walk_6, &grot_walk_7, nullptr
};
static const lv_image_dsc_t* adult_exc_happy[] = {
    &grot_jumping_0, &grot_jumping_1, &grot_jumping_2,
    &grot_jumping_3, &grot_jumping_4, nullptr
};

// --- Adult / Senior — Good quality ---
static const lv_image_dsc_t* adult_good_idle[] = {
    &grot_walk_0, &grot_walk_1, &grot_walk_2, &grot_walk_3, nullptr
};
static const lv_image_dsc_t* adult_good_happy[] = {
    &grot_wave_0, &grot_wave_1, &grot_wave_2, &grot_wave_3, nullptr
};

// --- Adult / Senior — Tired quality ---
static const lv_image_dsc_t* adult_tired_idle[] = {
    &grot_0, &grot_1, nullptr
};

// --- Left-facing walk tables (mirror of right-facing; same frame counts) ---
static const lv_image_dsc_t* child_idle_left[] = {
    &grot_walk_left_0, &grot_walk_left_1, &grot_walk_left_2, &grot_walk_left_3, nullptr
};
static const lv_image_dsc_t* adult_exc_idle_left[] = {
    &grot_walk_left_0, &grot_walk_left_1, &grot_walk_left_2, &grot_walk_left_3,
    &grot_walk_left_4, &grot_walk_left_5, &grot_walk_left_6, &grot_walk_left_7, nullptr
};
static const lv_image_dsc_t* adult_good_idle_left[] = {
    &grot_walk_left_0, &grot_walk_left_1, &grot_walk_left_2, &grot_walk_left_3, nullptr
};

// --- Shared override states ---
static const lv_image_dsc_t* shared_sick[] = {
    &grot_thinks_4, nullptr
};
static const lv_image_dsc_t* shared_sleeping[] = {
    &grot_blink_0, &grot_blink_1, nullptr
};
// Reuse latter half of jump for the spinning dizzy look
static const lv_image_dsc_t* shared_dizzy[] = {
    &grot_jumping_4, &grot_jumping_5, &grot_jumping_6, &grot_jumping_7, nullptr
};
static const lv_image_dsc_t* shared_dead[] = {
    &grot_thinks_4, nullptr
};

// =============================================================================
// Lookup helpers
// =============================================================================

static uint8_t count_frames(const lv_image_dsc_t* const* table) {
    uint8_t n = 0;
    while (table[n] != nullptr) n++;
    return n;
}

const lv_image_dsc_t* const* grot_get_frames(uint8_t stage_u8, uint8_t emotion_u8, uint8_t quality_u8) {
    auto stage   = static_cast<LifeStage>(stage_u8);
    auto emotion = static_cast<GrotEmotion>(emotion_u8);
    auto quality = static_cast<EvoQuality>(quality_u8);

    // Shared states override stage-specific frames
    switch (emotion) {
        case GrotEmotion::DEAD:     return shared_dead;
        case GrotEmotion::SICK:     return shared_sick;
        case GrotEmotion::SLEEPING: return shared_sleeping;
        case GrotEmotion::DIZZY:    return shared_dizzy;
        default: break;
    }

    switch (stage) {
        case LifeStage::EGG:
            return egg_idle;

        case LifeStage::BABY:
            if (emotion == GrotEmotion::HAPPY) return baby_happy;
            if (emotion == GrotEmotion::SAD)   return baby_sad;
            return baby_idle;

        case LifeStage::CHILD:
        case LifeStage::TEEN:
            if (emotion == GrotEmotion::HAPPY) return child_happy;
            if (emotion == GrotEmotion::SAD)   return child_sad;
            if (emotion == GrotEmotion::SICK)  return child_sick;
            return child_idle;

        case LifeStage::ADULT:
        case LifeStage::SENIOR:
            if (quality == EvoQuality::EXCELLENT) {
                if (emotion == GrotEmotion::HAPPY) return adult_exc_happy;
                return adult_exc_idle;
            } else if (quality == EvoQuality::GOOD) {
                if (emotion == GrotEmotion::HAPPY) return adult_good_happy;
                return adult_good_idle;
            } else {
                return adult_tired_idle;
            }

        default:
            return shared_dead;
    }
}

uint8_t grot_get_frame_count(uint8_t stage, uint8_t emotion, uint8_t quality) {
    return count_frames(grot_get_frames(stage, emotion, quality));
}

const lv_image_dsc_t* const* grot_get_walk_left_frames(uint8_t stage_u8, uint8_t quality_u8) {
    auto stage   = static_cast<LifeStage>(stage_u8);
    auto quality = static_cast<EvoQuality>(quality_u8);
    switch (stage) {
        case LifeStage::CHILD:
        case LifeStage::TEEN:
            return child_idle_left;
        case LifeStage::ADULT:
        case LifeStage::SENIOR:
            if (quality == EvoQuality::EXCELLENT) return adult_exc_idle_left;
            if (quality == EvoQuality::GOOD)      return adult_good_idle_left;
            return nullptr;  // TIRED doesn't walk
        default:
            return nullptr;
    }
}
