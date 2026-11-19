/**
 * touch.h — TTP223 capacitive touch input with debounce.
 *
 * Emits exactly one clean event per physical touch (rising edge,
 * debounced). The event is consumed by the caller.
 */
#pragma once

#include <Arduino.h>

class TouchSensor {
public:
    void begin();

    /// Sample the pin and run the debounce state machine. Call every loop.
    void update();

    /// Returns true once per clean touch, then resets the flag.
    bool consumeTouchEvent();

    bool isTouched() const { return _stableTouched; }

private:
    bool     _initialized   = false;
    bool     _stableTouched = false;
    bool     _lastRaw       = false;
    bool     _pendingEvent  = false;
    uint32_t _lastChangeMs  = 0;
};

extern TouchSensor touchSensor;
