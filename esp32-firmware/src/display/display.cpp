/**
 * display.cpp — face renderer for the 0.96" SSD1306 OLED.
 *
 * Rendering constants (pixels) are defined once below; hardware pins
 * live in config.h. One-shot animations override the base face for a
 * short, millis()-bounded window, then the base face is restored.
 */
#include "display.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "../config.h"

FaceDisplay display;

// Rendering constants
namespace {

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

constexpr int16_t  EYE_CENTER_Y        = 26;   // vertical center of the eyes
constexpr int16_t  LEFT_EYE_X          = 40;   // left eye center
constexpr int16_t  RIGHT_EYE_X         = 88;   // right eye center
constexpr int16_t  EYE_RADIUS          = 11;
constexpr int16_t  EYE_RECT_W          = 20;
constexpr int16_t  EYE_RECT_H          = 26;
constexpr int16_t  GLANCE_OFFSET_PX    = 14;
constexpr int16_t  TILT_OFFSET_PX      = 8;
constexpr int16_t  SPEECH_BAR_Y        = 54;
constexpr int16_t  LINK_DOT_X          = 122;
constexpr int16_t  LINK_DOT_Y          = 4;
constexpr int16_t  LINK_DOT_R          = 2;

constexpr uint32_t ANIM_MS_BLINK      = 130;
constexpr uint32_t ANIM_MS_GLANCE     = 700;
constexpr uint32_t ANIM_MS_SMILE      = 1400;
constexpr uint32_t ANIM_MS_TILT       = 900;

void drawOpenEye(int16_t cx, int16_t cy) {
    oled.fillCircle(cx, cy, EYE_RADIUS, SSD1306_WHITE);
}

void drawHappyEye(int16_t cx, int16_t cy) {
    // Upper half-circle: an open eye with the bottom masked -> "^" arc look.
    oled.fillCircle(cx, cy + 4, EYE_RADIUS, SSD1306_WHITE);
    oled.fillRect(cx - EYE_RADIUS - 2, cy + 4, (EYE_RADIUS + 2) * 2, EYE_RADIUS + 4, SSD1306_BLACK);
    oled.drawLine(cx - EYE_RADIUS, cy + 4, cx + EYE_RADIUS, cy + 4, SSD1306_WHITE);
}

void drawClosedEye(int16_t cx, int16_t cy) {
    oled.fillRect(cx - EYE_RADIUS, cy - 2, EYE_RADIUS * 2, 4, SSD1306_WHITE);
}

void drawLiddedEye(int16_t cx, int16_t cy) {
    // Sleepy: thin slit with a lid line on top.
    oled.fillRect(cx - EYE_RADIUS, cy - 1, EYE_RADIUS * 2, 3, SSD1306_WHITE);
    oled.drawLine(cx - EYE_RADIUS, cy - 6, cx + EYE_RADIUS, cy - 6, SSD1306_WHITE);
}

void drawScaredEye(int16_t cx, int16_t cy) {
    oled.drawCircle(cx, cy, EYE_RADIUS, SSD1306_WHITE);
    oled.fillCircle(cx, cy, 4, SSD1306_WHITE);
}

void drawCuriousEye(int16_t cx, int16_t cy, int16_t r) {
    oled.fillCircle(cx, cy, r, SSD1306_WHITE);
}

void drawThinkingEye(int16_t cx, int16_t cy) {
    // Eyes looking up-right with a small pupil.
    oled.drawCircle(cx, cy, EYE_RADIUS, SSD1306_WHITE);
    oled.fillCircle(cx + 4, cy - 4, 5, SSD1306_WHITE);
}

} // namespace

// Public API
void FaceDisplay::begin() {
    if (_initialized) return;

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, OLED_I2C_FREQ_HZ);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[display] SSD1306 init FAILED (check wiring/address)");
        return;
    }
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    // Boot splash
    oled.setCursor(20, 24);
    oled.print(ROBOT_NAME);
    oled.setCursor(20, 36);
    oled.print("v" FIRMWARE_VERSION);
    oled.display();

    _initialized = true;
    Serial.println("[display] SSD1306 ready");
}

void FaceDisplay::setFace(Face face) {
    if (!_initialized || _face == face) return;
    _face = face;
    _anim = FaceAnim::NONE;
    render();
}

void FaceDisplay::triggerAnimation(FaceAnim anim) {
    if (!_initialized || anim == FaceAnim::NONE) return;

    uint32_t now = millis();
    _anim = anim;
    switch (anim) {
        case FaceAnim::BLINK:      _animEndMs = now + ANIM_MS_BLINK;  break;
        case FaceAnim::LOOK_LEFT:
        case FaceAnim::LOOK_RIGHT: _animEndMs = now + ANIM_MS_GLANCE; break;
        case FaceAnim::SMILE:      _animEndMs = now + ANIM_MS_SMILE;  break;
        case FaceAnim::TILT:       _animEndMs = now + ANIM_MS_TILT;   break;
        default:                   _animEndMs = now;                  break;
    }
    render();
}

void FaceDisplay::setSpeech(const char* text, uint32_t durationMs) {
    if (!_initialized) return;

    const char* src = (text != nullptr) ? text : "";
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out < sizeof(_speech) - 1; ++i) {
        const char c = src[i];
        _speech[out++] = (c >= 32 && c <= 126) ? c : ' ';
    }
    while (out > 0 && _speech[out - 1] == ' ') --out;
    _speech[out] = '\0';

    _speechEndMs = millis() + durationMs;
    render();
}

void FaceDisplay::setLink(bool online) {
    if (!_initialized || _linkOnline == online) return;
    _linkOnline = online;
    render();
}

void FaceDisplay::update() {
    if (!_initialized) return;

    uint32_t now = millis();
    bool dirty = false;

    if (_anim != FaceAnim::NONE && static_cast<int32_t>(now - _animEndMs) >= 0) {
        _anim = FaceAnim::NONE;
        dirty = true;
    }
    if (_speech[0] != '\0' && static_cast<int32_t>(now - _speechEndMs) >= 0) {
        _speech[0] = '\0';
        dirty = true;
    }
    if (dirty) render();
}

// Rendering
void FaceDisplay::render() {
    oled.clearDisplay();
    drawEyes();
    if (_speech[0] != '\0') drawSpeechBar();
    drawLinkDot();
    oled.display();
}

void FaceDisplay::drawEyes() {
    const bool animating = (_anim != FaceAnim::NONE);

    // One-shot animations take priority over the base face.
    switch (_anim) {
        case FaceAnim::BLINK:
            drawClosedEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawClosedEye(RIGHT_EYE_X, EYE_CENTER_Y);
            return;
        case FaceAnim::LOOK_LEFT:
            drawOpenEye(LEFT_EYE_X - GLANCE_OFFSET_PX, EYE_CENTER_Y);
            drawOpenEye(RIGHT_EYE_X - GLANCE_OFFSET_PX, EYE_CENTER_Y);
            return;
        case FaceAnim::LOOK_RIGHT:
            drawOpenEye(LEFT_EYE_X + GLANCE_OFFSET_PX, EYE_CENTER_Y);
            drawOpenEye(RIGHT_EYE_X + GLANCE_OFFSET_PX, EYE_CENTER_Y);
            return;
        case FaceAnim::SMILE:
            drawHappyEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawHappyEye(RIGHT_EYE_X, EYE_CENTER_Y);
            return;
        case FaceAnim::TILT:
            drawOpenEye(LEFT_EYE_X, EYE_CENTER_Y - TILT_OFFSET_PX);
            drawOpenEye(RIGHT_EYE_X, EYE_CENTER_Y + TILT_OFFSET_PX);
            return;
        case FaceAnim::NONE:
        default:
            break;
    }

    if (animating) return;

    // Base faces
    switch (_face) {
        case Face::HAPPY:
            drawHappyEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawHappyEye(RIGHT_EYE_X, EYE_CENTER_Y);
            break;
        case Face::CURIOUS:
            drawCuriousEye(LEFT_EYE_X, EYE_CENTER_Y, EYE_RADIUS);
            drawCuriousEye(RIGHT_EYE_X, EYE_CENTER_Y + 3, EYE_RADIUS - 5);
            break;
        case Face::SLEEPY:
            drawLiddedEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawLiddedEye(RIGHT_EYE_X, EYE_CENTER_Y);
            break;
        case Face::THINKING:
            drawThinkingEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawThinkingEye(RIGHT_EYE_X, EYE_CENTER_Y);
            // animated "..." dots near the temple
            oled.fillCircle(108, 10, 2, SSD1306_WHITE);
            oled.fillCircle(116, 16, 2, SSD1306_WHITE);
            oled.fillCircle(122, 23, 2, SSD1306_WHITE);
            break;
        case Face::SCARED:
            drawScaredEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawScaredEye(RIGHT_EYE_X, EYE_CENTER_Y);
            break;
        case Face::SLEEP:
            drawClosedEye(LEFT_EYE_X, EYE_CENTER_Y);
            drawClosedEye(RIGHT_EYE_X, EYE_CENTER_Y);
            oled.setCursor(104, 8);
            oled.print("z");
            break;
        case Face::NEUTRAL:
        default:
            oled.fillRoundRect(LEFT_EYE_X - EYE_RECT_W / 2, EYE_CENTER_Y - EYE_RECT_H / 2,
                               EYE_RECT_W, EYE_RECT_H, 6, SSD1306_WHITE);
            oled.fillRoundRect(RIGHT_EYE_X - EYE_RECT_W / 2, EYE_CENTER_Y - EYE_RECT_H / 2,
                               EYE_RECT_W, EYE_RECT_H, 6, SSD1306_WHITE);
            break;
    }
}

void FaceDisplay::drawSpeechBar() {
    oled.fillRect(0, SPEECH_BAR_Y, OLED_WIDTH, OLED_HEIGHT - SPEECH_BAR_Y, SSD1306_BLACK);
    oled.drawRect(0, SPEECH_BAR_Y, OLED_WIDTH, OLED_HEIGHT - SPEECH_BAR_Y, SSD1306_WHITE);
    oled.setCursor(2, SPEECH_BAR_Y + 2);
    char line[22];
    strncpy(line, _speech, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    oled.print(line);
}

void FaceDisplay::drawLinkDot() {
    if (_linkOnline) {
        oled.fillCircle(LINK_DOT_X, LINK_DOT_Y, LINK_DOT_R, SSD1306_WHITE);
    } else {
        oled.drawCircle(LINK_DOT_X, LINK_DOT_Y, LINK_DOT_R, SSD1306_WHITE);
    }
}
