#include "buttons.h"
#include "../config.h"
#include <Arduino.h>

// Ring buffer capacity (power of 2 for cheap modulo)
static const uint8_t QUEUE_CAP = 8;
static ButtonEvent _queue[QUEUE_CAP];
static uint8_t _head = 0, _tail = 0;

struct BtnState {
    uint8_t  pin;
    bool     lastRaw;
    bool     debounced;
    uint32_t lastChangeMs;
    uint32_t pressedAtMs;
    bool     longFired;
};

static BtnState _btns[3] = {
    {PIN_BTN_A, true, true, 0, 0, false},
    {PIN_BTN_B, true, true, 0, 0, false},
    {PIN_BTN_C, true, true, 0, 0, false},
};

static void enqueue(ButtonEvent e) {
    uint8_t next = (_tail + 1) & (QUEUE_CAP - 1);
    if (next != _head) {  // drop if full
        _queue[_tail] = e;
        _tail = next;
    }
}

void buttons_init() {
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
    pinMode(PIN_BTN_C, INPUT_PULLUP);
}

void buttons_update() {
    uint32_t now = millis();

    for (int i = 0; i < 3; i++) {
        bool raw = (digitalRead(_btns[i].pin) == LOW);  // active-low with pull-up

        // Detect raw edge
        if (raw != _btns[i].lastRaw) {
            _btns[i].lastRaw = raw;
            _btns[i].lastChangeMs = now;
        }

        // Apply debounce window
        if ((now - _btns[i].lastChangeMs) < BTN_DEBOUNCE_MS) continue;

        bool prev = _btns[i].debounced;
        _btns[i].debounced = raw;

        if (!prev && raw) {
            // Pressed
            _btns[i].pressedAtMs = now;
            _btns[i].longFired   = false;
        }

        if (prev && !raw && !_btns[i].longFired) {
            // Released without long-press firing → short press
            // ButtonEvent: A_PRESS=1, B_PRESS=2, C_PRESS=3
            enqueue(static_cast<ButtonEvent>(i + 1));
        }

        if (raw && !_btns[i].longFired &&
            (now - _btns[i].pressedAtMs) >= BTN_LONG_PRESS_MS) {
            // Long press threshold crossed
            _btns[i].longFired = true;
            // A_LONG=4, B_LONG=5, C_LONG=6
            enqueue(static_cast<ButtonEvent>(i + 4));
        }
    }
}

ButtonEvent buttons_get_event() {
    if (_head == _tail) return ButtonEvent::NONE;
    ButtonEvent e = _queue[_head];
    _head = (_head + 1) & (QUEUE_CAP - 1);
    return e;
}
