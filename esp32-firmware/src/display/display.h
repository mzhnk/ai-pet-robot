/**
 * display.h — SSD1306 128x64 face system.
 *
 * All drawing is non-blocking: one-shot animations (blink, glance, ...)
 * are driven by update() using millis() deadlines. Redraws happen only
 * on state changes, keeping the main loop responsive.
 */
#pragma once

#include <Arduino.h>

enum class Face : uint8_t {
    NEUTRAL = 0,
    HAPPY,
    CURIOUS,
    SLEEPY,
    THINKING,
    SCARED,
    SLEEP,
};

enum class FaceAnim : uint8_t {
    NONE = 0,
    BLINK,
    LOOK_LEFT,
    LOOK_RIGHT,
    SMILE,
    TILT,
};

class FaceDisplay {
public:
    void begin();

    void setFace(Face face);
    Face face() const { return _face; }

    /// Play a one-shot animation on top of the current face.
    void triggerAnimation(FaceAnim anim);

    /// Short message on the bottom bar (auto-clears after durationMs).
    void setSpeech(const char* text, uint32_t durationMs = 3000);

    /// Connectivity indicator dot (bottom-right corner).
    void setLink(bool online);

    /// Drive animation/speech deadlines; call every loop iteration.
    void update();

private:
    void render();
    void drawEyes();
    void drawSpeechBar();
    void drawLinkDot();

    bool     _initialized = false;
    Face     _face        = Face::NEUTRAL;
    FaceAnim _anim        = FaceAnim::NONE;
    uint32_t _animEndMs   = 0;
    uint32_t _speechEndMs = 0;
    char     _speech[32]  = {0};
    bool     _linkOnline  = false;
};

extern FaceDisplay display;
