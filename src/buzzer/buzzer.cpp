#include "buzzer.h"
#include "../config.h"
#include <Arduino.h>

// =============================================================================
// Async state
// =============================================================================
static const MelodyNote* _mel      = nullptr;
static uint8_t           _mel_len  = 0;
static uint8_t           _mel_idx  = 0;
static uint32_t          _mel_next = 0;
static bool              _playing  = false;
static bool              _muted    = false;
static void            (*_play_cb)(void) = nullptr;

// =============================================================================
// Pre-composed melodies
// =============================================================================

// Feed: short ascending two-note chirp
const MelodyNote MELODY_FEED[] = {
    {Tone::G4, 80}, {Tone::C5, 120}
};
const uint8_t MELODY_FEED_LEN = 2;

// Happy: bright three-note ascending
const MelodyNote MELODY_HAPPY[] = {
    {Tone::C5, 80}, {Tone::E5, 80}, {Tone::G5, 150}
};
const uint8_t MELODY_HAPPY_LEN = 3;

// Alert: repeated beep-beep
const MelodyNote MELODY_ALERT[] = {
    {Tone::A4, 200}, {Tone::SIL, 100},
    {Tone::A4, 200}, {Tone::SIL, 100},
    {Tone::A4, 200}
};
const uint8_t MELODY_ALERT_LEN = 5;

// Evolution: ascending fanfare
const MelodyNote MELODY_EVOLVE[] = {
    {Tone::C4, 80}, {Tone::E4, 80}, {Tone::G4, 80}, {Tone::C5, 80},
    {Tone::SIL, 60},
    {Tone::C5, 60}, {Tone::D5, 60}, {Tone::E5, 200}
};
const uint8_t MELODY_EVOLVE_LEN = 8;

// Sick: descending droopy
const MelodyNote MELODY_SICK[] = {
    {Tone::G4, 150}, {Tone::E4, 150}, {Tone::C4, 250}
};
const uint8_t MELODY_SICK_LEN = 3;

// Dead: descending funeral arpeggio
const MelodyNote MELODY_DEAD[] = {
    {Tone::E5, 200}, {Tone::SIL, 80},
    {Tone::C5, 200}, {Tone::SIL, 80},
    {Tone::G4, 200}, {Tone::SIL, 80},
    {Tone::C4, 400}
};
const uint8_t MELODY_DEAD_LEN = 7;

// Dizzy: wobbly rapid oscillation
const MelodyNote MELODY_DIZZY[] = {
    {Tone::G4, 60}, {Tone::E4, 60}, {Tone::G4, 60}, {Tone::E4, 60},
    {Tone::D4, 60}, {Tone::F4, 60}, {Tone::D4, 80}
};
const uint8_t MELODY_DIZZY_LEN = 7;

// Medicine: clean two-tone positive
const MelodyNote MELODY_MEDICINE[] = {
    {Tone::D5, 100}, {Tone::SIL, 50}, {Tone::E5, 150}
};
const uint8_t MELODY_MEDICINE_LEN = 3;

// Boot jingle: cheerful ascending four-note
const MelodyNote MELODY_BOOT[] = {
    {Tone::C4, 100}, {Tone::E4, 100}, {Tone::G4, 100}, {Tone::C5, 200},
    {Tone::SIL, 80},  {Tone::G4, 80},  {Tone::C5, 300}
};
const uint8_t MELODY_BOOT_LEN = 7;

// =============================================================================
// Driver implementation
// =============================================================================

void buzzer_set_muted(bool muted)    { _muted   = muted; }
void buzzer_set_play_cb(void (*cb)(void)) { _play_cb = cb;    }

void buzzer_init() {
    ledcSetup(BUZZER_LEDC_CH, 1000, BUZZER_LEDC_RES);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
    ledcWrite(BUZZER_LEDC_CH, 0);  // silent
}

void buzzer_tone(uint16_t freq, uint32_t durationMs) {
    if (freq == 0) {
        ledcWrite(BUZZER_LEDC_CH, 0);
    } else {
        ledcWriteTone(BUZZER_LEDC_CH, freq);
        ledcWrite(BUZZER_LEDC_CH, BUZZER_DUTY);  // 50% duty cycle
    }
    delay(durationMs);
    ledcWrite(BUZZER_LEDC_CH, 0);
}

void buzzer_play_async(const MelodyNote* melody, uint8_t len) {
    if (_play_cb) _play_cb();   // fire visual flash regardless of mute state
    if (_muted) return;
    _mel     = melody;
    _mel_len = len;
    _mel_idx = 0;
    _playing = true;
    _mel_next = millis();  // start immediately
}

void buzzer_update() {
    if (!_playing || !_mel) return;

    uint32_t now = millis();
    if (now < _mel_next) return;

    if (_mel_idx >= _mel_len) {
        _playing = false;
        ledcWrite(BUZZER_LEDC_CH, 0);
        return;
    }

    const MelodyNote& n = _mel[_mel_idx++];
    if (n.freq == 0) {
        ledcWrite(BUZZER_LEDC_CH, 0);
    } else {
        ledcWriteTone(BUZZER_LEDC_CH, n.freq);
        ledcWrite(BUZZER_LEDC_CH, BUZZER_DUTY);
    }
    _mel_next = now + n.durationMs;
}

void buzzer_stop() {
    _playing = false;
    ledcWrite(BUZZER_LEDC_CH, 0);
}

bool buzzer_is_playing() {
    return _playing;
}
