/**
 * sound.cpp — non-blocking buzzer sequences.
 *
 * NOTE_FREQ/DUR tables live here (musical data, not hardware config).
 * ledcWriteTone() drives the passive buzzer on its own LEDC channel.
 */
#include "sound.h"

#include "../config.h"

SoundPlayer sound;

namespace {

// Note sequences: {frequencies Hz}, {durations ms}, terminated implicitly by count.
// duration 0 entries act as short rests (frequency 0 = silence).
constexpr uint16_t SEQ_STARTUP_FREQ[]  = {523, 659, 784, 1047};       // C5 E5 G5 C6
constexpr uint8_t  SEQ_STARTUP_DUR[]   = {90,  90,  90,  160};
constexpr uint16_t SEQ_HAPPY_FREQ[]    = {659, 784, 659, 784, 1047};  // E5 G5 E5 G5 C6
constexpr uint8_t  SEQ_HAPPY_DUR[]     = {70,  70,  70,  70,  140};
constexpr uint16_t SEQ_THINK_FREQ[]    = {523, 0,   523, 0};          // two soft blips
constexpr uint8_t  SEQ_THINK_DUR[]     = {60,  80,  60,  0};
constexpr uint16_t SEQ_ERROR_FREQ[]    = {220, 0,   165, 0};          // low double buzz
constexpr uint8_t  SEQ_ERROR_DUR[]     = {160, 60,  220, 0};
constexpr uint16_t SEQ_BEEP_FREQ[]     = {880};
constexpr uint8_t  SEQ_BEEP_DUR[]      = {80};

} // namespace

void SoundPlayer::begin() {
    if (_initialized) return;
    ledcSetup(BUZZER_CHANNEL, BUZZER_PWM_FREQ_HZ, BUZZER_PWM_RESOLUTION);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
    applyTone(0);
    _initialized = true;
    Serial.println("[sound] buzzer initialized");
}

void SoundPlayer::play(SoundId id) {
    switch (id) {
        case SoundId::STARTUP:  startSequence(SEQ_STARTUP_FREQ, SEQ_STARTUP_DUR, 4); break;
        case SoundId::HAPPY:    startSequence(SEQ_HAPPY_FREQ,  SEQ_HAPPY_DUR,  5);   break;
        case SoundId::THINKING: startSequence(SEQ_THINK_FREQ,  SEQ_THINK_DUR,  4);   break;
        case SoundId::ERROR:    startSequence(SEQ_ERROR_FREQ,  SEQ_ERROR_DUR,  4);   break;
        case SoundId::BEEP:
        default:                startSequence(SEQ_BEEP_FREQ,   SEQ_BEEP_DUR,   1);   break;
    }
}

void SoundPlayer::beep(uint16_t freqHz, uint32_t durationMs) {
    if (!_initialized) return;
    if (durationMs == 0 || durationMs > 1000) durationMs = 100;   // sanity bound
    _freq      = freqHz;
    _noteEndMs = millis() + durationMs;
    _playing   = true;
    _singleShot = true;
    applyTone(freqHz);
}

void SoundPlayer::stop() {
    _playing = false;
    _singleShot = false;
    _step = 0;
    _stepCount = 0;
    _seqFreqs = nullptr;
    _seqDurations = nullptr;
    applyTone(0);
}

void SoundPlayer::update() {
    if (!_initialized || !_playing) return;
    if (static_cast<int32_t>(millis() - _noteEndMs) < 0) return;

    if (_singleShot) {           // simple beep finished
        stop();
        return;
    }

    _step++;
    if (_step >= _stepCount) {   // sequence finished
        stop();
        return;
    }

    uint16_t f = _seqFreqs[_step];
    _noteEndMs = millis() + _seqDurations[_step];
    applyTone(f);
}

// ------------------------------------------------------------
// Internals
// ------------------------------------------------------------

void SoundPlayer::startSequence(const uint16_t* freqs, const uint8_t* durationsMs, uint8_t count) {
    if (!_initialized || freqs == nullptr || durationsMs == nullptr || count == 0) return;
    _seqFreqs     = freqs;
    _seqDurations = durationsMs;
    _stepCount    = count;
    _step         = 0;
    _playing      = true;
    _singleShot   = false;
    _noteEndMs    = millis() + durationsMs[0];
    applyTone(freqs[0]);
}

void SoundPlayer::applyTone(uint16_t freqHz) {
    _freq = freqHz;
    ledcWriteTone(BUZZER_CHANNEL, freqHz);
}
