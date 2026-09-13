/**
 * touch.cpp — debounced TTP223 reader.
 *
 * A level change is only accepted after the pin has been stable for
 * TOUCH_DEBOUNCE_MS. This filters the soft edges of capacitive sensors
 * and guarantees one event per touch.
 */
#include "touch.h"

#include "../config.h"

TouchSensor touchSensor;

void TouchSensor::begin() {
    if (_initialized) return;
    pinMode(PIN_TOUCH_OUT, INPUT);
    _lastRaw       = (digitalRead(PIN_TOUCH_OUT) == TOUCH_ACTIVE_LEVEL);
    _stableTouched = _lastRaw;
    _lastChangeMs  = millis();
    _initialized   = true;
    Serial.println("[touch] TTP223 initialized");
}

void TouchSensor::update() {
    if (!_initialized) return;

    bool raw = (digitalRead(PIN_TOUCH_OUT) == TOUCH_ACTIVE_LEVEL);
    uint32_t now = millis();

    if (raw != _lastRaw) {
        _lastRaw      = raw;
        _lastChangeMs = now;
        return;                     // signal still moving; wait for stability
    }

    if (raw != _stableTouched &&
        static_cast<int32_t>(now - (_lastChangeMs + TOUCH_DEBOUNCE_MS)) >= 0) {
        _stableTouched = raw;
        if (raw) _pendingEvent = true;   // clean rising edge -> one event
    }
}

bool TouchSensor::consumeTouchEvent() {
    bool ev = _pendingEvent;
    _pendingEvent = false;
    return ev;
}
