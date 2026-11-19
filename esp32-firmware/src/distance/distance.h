/**
 * distance.h — HC-SR04 ultrasonic rangefinder, fully non-blocking.
 *
 * The echo pulse is captured by a pin-change ISR (rise/fall timestamps),
 * so update() never busy-waits. Filtering = median-of-3 + EMA.
 * Obstacle flags use hysteresis (ENTER < CLEAR) to avoid flapping.
 */
#pragma once

#include <Arduino.h>

#include "../config.h"

class DistanceSensor {
public:
    void begin();

    /// Paces trigger pulses and consumes completed echo pulses. Call every loop.
    void update();

    bool hasReading() const;          // a filtered value exists and is fresh
    float distanceCm() const;         // filtered value (invalid until hasReading)
    bool isStale() const;             // no fresh echo for SONAR_STALE_MS

    /// Hysteresis-applied obstacle state (true only while an obstacle is close).
    bool obstacleActive() const { return _obstacleActive; }
    /// Emergency-close: inside OBSTACLE_CLOSE_CM (stop immediately).
    bool obstacleClose() const { return hasReading() && _filteredCm <= OBSTACLE_CLOSE_CM; }

private:
    static void echoIsr();

    void triggerPulse();
    void consumeEchoPulse();
    void pushSample(float cm);
    void updateObstacleFlags();

    static DistanceSensor* _instance;   // ISR trampoline

    volatile bool     _pulseComplete = false;
    volatile uint32_t _riseUs        = 0;   // echo rising-edge timestamp
    volatile uint32_t _pulseWidthUs  = 0;  // echo low->high pulse width

    bool     _initialized     = false;
    float    _filteredCm      = 0.0f;
    bool     _hasFiltered     = false;
    uint32_t _lastSampleMs    = 0;
    uint32_t _lastEchoMs      = 0;
    float    _medianBuf[SONAR_MEDIAN_WINDOW] = {0};
    uint8_t  _medianCount     = 0;
    bool     _obstacleActive  = false;
};

extern DistanceSensor sonar;
