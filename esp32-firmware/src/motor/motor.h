/**
 * motor.h — TB6612FNG dual motor driver with bounded, auto-stopping motion.
 *
 * Safety contract:
 *  - Default state is STOP.
 *  - Every motion command carries a duration and always self-expires.
 *  - update() must be called from loop(); it enforces deadlines via millis().
 *  - Explicit stop() takes effect immediately.
 */
#pragma once

#include <Arduino.h>

enum class MotorDirection : uint8_t {
    STOP = 0,
    FORWARD,
    BACKWARD,
    TURN_LEFT,
    TURN_RIGHT,
};

class MotorDriver {
public:
    void begin();

    /// Call every loop iteration; auto-stops when the motion deadline expires.
    void update();

    /// Start a bounded motion. Returns false if the request was rejected/clamped.
    bool startMotion(MotorDirection dir, uint8_t speed, uint32_t durationMs);

    /// Immediate stop (also clears any pending deadline).
    void stop();

    bool isMoving() const { return _direction != MotorDirection::STOP; }
    MotorDirection direction() const { return _direction; }
    uint32_t remainingMs() const;

private:
    void setLeftWheel(bool forward, uint8_t speed);
    void setRightWheel(bool forward, uint8_t speed);
    void writeDuty(uint8_t ledcChannel, uint8_t duty);
    static uint8_t clampSpeed(uint8_t speed);
    static uint32_t clampDuration(uint32_t durationMs);

    bool           _initialized = false;
    MotorDirection _direction   = MotorDirection::STOP;
    uint32_t       _deadlineMs  = 0;   // millis() deadline for auto-stop
    uint8_t        _speed       = 0;
};

extern MotorDriver motors;
