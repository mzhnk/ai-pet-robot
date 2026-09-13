/**
 * sound.h — passive buzzer player (non-blocking note sequences).
 *
 * Sequences are scheduled with millis() deadlines inside update();
 * nothing here ever blocks the main loop.
 */
#pragma once

#include <Arduino.h>

enum class SoundId : uint8_t {
    BEEP = 0,
    STARTUP,
    HAPPY,
    THINKING,
    ERROR,
};

class SoundPlayer {
public:
    void begin();
    void update();

    void play(SoundId id);
    void beep(uint16_t freqHz, uint32_t durationMs);
    void stop();                    // silence immediately

    bool isPlaying() const { return _playing; }

private:
    void startSequence(const uint16_t* freqs, const uint8_t* durationsMs, uint8_t count);
    void applyTone(uint16_t freqHz);

    bool     _initialized = false;
    bool     _playing     = false;
    bool     _singleShot  = false;    // simple beep mode
    uint8_t  _step        = 0;
    uint8_t  _stepCount   = 0;
    uint16_t _freq        = 0;
    uint32_t _noteEndMs   = 0;
    const uint16_t* _seqFreqs = nullptr;
    const uint8_t*  _seqDurations = nullptr;
};

extern SoundPlayer sound;
