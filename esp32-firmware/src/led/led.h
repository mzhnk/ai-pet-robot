/**
 * led.h — WS2812B 8-LED strip animations (non-blocking).
 */
#pragma once

#include <Arduino.h>

enum class LedMode : uint8_t {
    OFF = 0,
    SOLID,
    PULSE,      // gentle breathing in the current color
    THINKING,   // slow blue breathing
    HAPPY,      // fast rainbow spin
    ERROR,      // red blink
};

class LedStrip {
public:
    void begin();
    void update();

    void setMode(LedMode mode);
    LedMode mode() const { return _mode; }

    void setColor(uint8_t r, uint8_t g, uint8_t b);   // switches to SOLID
    void setBrightness(uint8_t brightness);           // 0..255 (global scale)

private:
    void renderFrame();

    bool     _initialized = false;
    LedMode  _mode        = LedMode::OFF;
    uint8_t  _brightness  = 0;
    uint8_t  _r = 0, _g = 0, _b = 0;
    uint32_t _phase       = 0;      // animation step counter
    uint32_t _lastFrameMs = 0;
};

extern LedStrip leds;
