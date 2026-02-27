#pragma once
#include <stdint.h>

// =============================================================================
// Buzzer driver — LEDC-based passive buzzer on GPIO 42
// =============================================================================

struct MelodyNote {
    uint16_t freq;        // Hz (0 = silence)
    uint16_t durationMs;
};

// Common musical frequencies
namespace Tone {
    constexpr uint16_t C4  = 262;
    constexpr uint16_t D4  = 294;
    constexpr uint16_t E4  = 330;
    constexpr uint16_t F4  = 349;
    constexpr uint16_t G4  = 392;
    constexpr uint16_t A4  = 440;
    constexpr uint16_t B4  = 494;
    constexpr uint16_t C5  = 523;
    constexpr uint16_t D5  = 587;
    constexpr uint16_t E5  = 659;
    constexpr uint16_t G5  = 784;
    constexpr uint16_t SIL = 0;
}

// Call once in setup()
void buzzer_init();

// Mute/unmute all playback (hardware still initialised)
void buzzer_set_muted(bool muted);

// Register a callback fired on every buzzer_play_async() call (even when muted).
// Use this to trigger a visual flash alongside sounds.
void buzzer_set_play_cb(void (*cb)(void));

// Blocking tone (for startup jingles during init)
void buzzer_tone(uint16_t freq, uint32_t durationMs);

// Non-blocking async melody — call buzzer_update() each loop() to advance
void buzzer_play_async(const MelodyNote* melody, uint8_t len);
void buzzer_update();
void buzzer_stop();

bool buzzer_is_playing();

// Pre-composed melodies
extern const MelodyNote MELODY_FEED[];
extern const MelodyNote MELODY_HAPPY[];
extern const MelodyNote MELODY_ALERT[];
extern const MelodyNote MELODY_EVOLVE[];
extern const MelodyNote MELODY_SICK[];
extern const MelodyNote MELODY_DEAD[];
extern const MelodyNote MELODY_DIZZY[];
extern const MelodyNote MELODY_MEDICINE[];
extern const MelodyNote MELODY_BOOT[];

extern const uint8_t MELODY_FEED_LEN;
extern const uint8_t MELODY_HAPPY_LEN;
extern const uint8_t MELODY_ALERT_LEN;
extern const uint8_t MELODY_EVOLVE_LEN;
extern const uint8_t MELODY_SICK_LEN;
extern const uint8_t MELODY_DEAD_LEN;
extern const uint8_t MELODY_DIZZY_LEN;
extern const uint8_t MELODY_MEDICINE_LEN;
extern const uint8_t MELODY_BOOT_LEN;
