/**
 * distance.cpp — HC-SR04 implementation (ISR echo capture, non-blocking).
 *
 * Timing contract: the sensor needs a 10 µs trigger pulse; the echo width
 * (µs) / 58 = distance in cm. A missing echo (out of range) means no new
 * sample arrives — the reading simply goes stale after SONAR_STALE_MS and
 * the last obstacle flag is kept (fail-safe: assume still blocked).
 */
#include "distance.h"

#include "../config.h"

DistanceSensor sonar;
DistanceSensor* DistanceSensor::_instance = nullptr;

namespace {

float medianOf3(float a, float b, float c) {
    if (a > b) { const float t = a; a = b; b = t; }
    if (b > c) { const float t = b; b = c; c = t; }
    return (a > b) ? a : b;   // middle value
}

} // namespace

void DistanceSensor::begin() {
    if (_initialized) return;
    _instance = this;

    pinMode(PIN_SONAR_TRIG, OUTPUT);
    digitalWrite(PIN_SONAR_TRIG, LOW);
    pinMode(PIN_SONAR_ECHO, INPUT);      // input-only pin: no pull-up available

    attachInterrupt(digitalPinToInterrupt(PIN_SONAR_ECHO), echoIsr, CHANGE);

    _initialized = true;
    Serial.println("[sonar] HC-SR04 ready");
}

void DistanceSensor::echoIsr() {
    if (_instance == nullptr) return;
    const uint32_t now = micros();
    if (digitalRead(PIN_SONAR_ECHO) == HIGH) {
        _instance->_riseUs = now;        // remember rise timestamp
    } else {
        _instance->_pulseWidthUs = now - _instance->_riseUs;
        _instance->_pulseComplete = true;
    }
}

void DistanceSensor::update() {
    if (!_initialized) return;
    consumeEchoPulse();

    const uint32_t now = millis();
    if (now - _lastSampleMs < SONAR_SAMPLE_MS) return;
    _lastSampleMs = now;
    triggerPulse();
}

void DistanceSensor::triggerPulse() {
    digitalWrite(PIN_SONAR_TRIG, LOW);
    delayMicroseconds(4);                // hw constraint: >2 µs low before trigger
    digitalWrite(PIN_SONAR_TRIG, HIGH);
    delayMicroseconds(10);               // hw constraint: 10 µs trigger pulse
    digitalWrite(PIN_SONAR_TRIG, LOW);
}

void DistanceSensor::consumeEchoPulse() {
    if (!_pulseComplete) return;
    noInterrupts();
    const uint32_t widthUs = _pulseWidthUs;
    _pulseComplete = false;
    interrupts();

    _lastEchoMs = millis();
    if (widthUs == 0 || widthUs > SONAR_ECHO_TIMEOUT_US) {
        // Phantom or over-long echo: clamp so filters decay gently.
        pushSample(SONAR_MAX_VALID_CM);
        return;
    }

    pushSample(static_cast<float>(widthUs) / 58.0f);
}

void DistanceSensor::pushSample(float cm) {
    if (cm <= 0.0f || cm > SONAR_MAX_VALID_CM) cm = SONAR_MAX_VALID_CM;

    _medianBuf[_medianCount % SONAR_MEDIAN_WINDOW] = cm;
    ++_medianCount;

    float median = cm;
    if (_medianCount >= SONAR_MEDIAN_WINDOW) {
        median = medianOf3(_medianBuf[0], _medianBuf[1], _medianBuf[2]);
    }

    if (!_hasFiltered) {
        _filteredCm  = median;
        _hasFiltered = true;
    } else {
        // EMA: newest sample weighted by SONAR_EMA_ALPHA_PCT.
        const float alpha = SONAR_EMA_ALPHA_PCT / 100.0f;
        _filteredCm += alpha * (median - _filteredCm);
    }
    updateObstacleFlags();
}

void DistanceSensor::updateObstacleFlags() {
    if (!_hasFiltered) return;
    // Hysteresis: enter near OBSTACLE_ENTER_CM, clear only past OBSTACLE_CLEAR_CM.
    if (_filteredCm <= OBSTACLE_ENTER_CM)            _obstacleActive = true;
    else if (_filteredCm >= OBSTACLE_CLEAR_CM)       _obstacleActive = false;
}

bool DistanceSensor::hasReading() const {
    return _hasFiltered && !isStale();
}

bool DistanceSensor::isStale() const {
    return (millis() - _lastEchoMs) > SONAR_STALE_MS;
}

float DistanceSensor::distanceCm() const {
    return _filteredCm;
}
