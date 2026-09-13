/**
 * led.cpp — WS2812B animations, millis()-driven.
 *
 * Animation frame rate and palette constants live here (rendering data);
 * hardware pin/count/brightness limits come from config.h.
 */
#include "led.h"

#include <Adafruit_NeoPixel.h>

#include "../config.h"

LedStrip leds;

namespace {

Adafruit_NeoPixel strip(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

constexpr uint32_t FRAME_INTERVAL_MS   = 40;    // ~25 fps is plenty for these effects
constexpr uint8_t  BRIGHTNESS_MAX      = 160;   // eye safety on a desktop robot
constexpr uint8_t  BRIGHTNESS_MIN      = 10;

// Default body color (soft cyan) — the robot's "ambient" identity.
constexpr uint8_t  DEFAULT_R = 0;
constexpr uint8_t  DEFAULT_G = 180;
constexpr uint8_t  DEFAULT_B = 220;

uint32_t wheel(uint8_t pos) {
    // Classic NeoPixel color wheel: 0-85 red->green, 86-170 green->blue, 171-255 blue->red.
    pos = 255 - pos;
    if (pos < 85)  return strip.Color(255 - pos * 3, 0, pos * 3);
    if (pos < 170) { pos -= 85; return strip.Color(0, pos * 3, 255 - pos * 3); }
    pos -= 170;
    return strip.Color(pos * 3, 255 - pos * 3, 0);
}

uint8_t triangleWave(uint32_t phase, uint32_t period) {
    uint32_t t = phase % period;
    uint32_t half = period / 2;
    return (t < half) ? static_cast<uint8_t>((t * 255) / half)
                      : static_cast<uint8_t>(((period - t) * 255) / half);
}

} // namespace

void LedStrip::begin() {
    if (_initialized) return;
    strip.begin();
    strip.setBrightness(BRIGHTNESS_MAX);          // hardware cap; user scale applied per-frame
    strip.show();
    _brightness = LED_DEFAULT_BRIGHTNESS;
    _r = DEFAULT_R; _g = DEFAULT_G; _b = DEFAULT_B;
    _initialized = true;
    Serial.println("[led] WS2812 initialized");
}

void LedStrip::setMode(LedMode mode) {
    if (!_initialized || _mode == mode) return;
    _mode = mode;
    _phase = 0;
    _lastFrameMs = 0;
    renderFrame();   // apply immediately (e.g. OFF must be instant)
}

void LedStrip::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _r = r; _g = g; _b = b;
    setMode(LedMode::SOLID);
}

void LedStrip::setBrightness(uint8_t brightness) {
    _brightness = (brightness > BRIGHTNESS_MAX) ? BRIGHTNESS_MAX : brightness;
}

void LedStrip::update() {
    if (!_initialized || _mode == LedMode::OFF || _mode == LedMode::SOLID) return;

    uint32_t now = millis();
    if (now - _lastFrameMs < FRAME_INTERVAL_MS) return;
    _lastFrameMs = now;
    _phase++;
    renderFrame();
}

void LedStrip::renderFrame() {
    if (!_initialized) return;

    switch (_mode) {
        case LedMode::OFF:
            strip.clear();
            break;

        case LedMode::SOLID: {
            if (_brightness == 0) {           // 0 == user wants darkness
                strip.clear();
                break;
            }
            uint8_t scale = (_brightness < BRIGHTNESS_MIN) ? BRIGHTNESS_MIN : _brightness;
            uint32_t c = strip.Color((_r * scale) / 255, (_g * scale) / 255, (_b * scale) / 255);
            strip.fill(c, 0, LED_COUNT);
            break;
        }

        case LedMode::PULSE: {
            uint8_t wave = triangleWave(_phase, 60);   // ~2.4 s breathing cycle
            uint8_t level = (uint8_t)((wave * _brightness) / 255);
            uint32_t c = strip.Color((_r * level) / 255, (_g * level) / 255, (_b * level) / 255);
            strip.fill(c, 0, LED_COUNT);
            break;
        }

        case LedMode::THINKING: {
            // Slow blue breathing — "the robot is processing".
            uint8_t wave = triangleWave(_phase, 90);
            uint8_t level = (uint8_t)((wave * _brightness) / 255);
            strip.fill(strip.Color(0, 0, level), 0, LED_COUNT);
            break;
        }

        case LedMode::HAPPY: {
            // Fast rainbow spin.
            uint8_t offset = (uint8_t)((_phase * 16) & 0xFF);
            for (uint8_t i = 0; i < LED_COUNT; i++) {
                strip.setPixelColor(i, wheel(offset + i * (256 / LED_COUNT)));
            }
            break;
        }

        case LedMode::ERROR: {
            // 2 Hz red blink.
            bool on = (_phase % 6) < 3;
            strip.fill(on ? strip.Color(_brightness, 0, 0) : 0, 0, LED_COUNT);
            break;
        }
    }

    strip.show();
}
