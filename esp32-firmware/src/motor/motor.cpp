/**
 * motor.cpp — TB6612FNG implementation.
 *
 * Wiring sense (robot viewed from above, motors pointing forward):
 *   AIN1/AIN2/PWMA -> LEFT wheel
 *   BIN1/BIN2/PWMB -> RIGHT wheel
 *
 * ESP32 LEDC: 2 channels (A/B) at MOTOR_PWM_FREQ_HZ, 8-bit resolution.
 */
#include "motor.h"

#include "../config.h"

MotorDriver motors;

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

void MotorDriver::begin() {
    if (_initialized) return;

    pinMode(PIN_MOTOR_STBY, OUTPUT);
    pinMode(PIN_MOTOR_AIN1, OUTPUT);
    pinMode(PIN_MOTOR_AIN2, OUTPUT);
    pinMode(PIN_MOTOR_BIN1, OUTPUT);
    pinMode(PIN_MOTOR_BIN2, OUTPUT);

    ledcSetup(MOTOR_PWM_CHANNEL_A, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RESOLUTION);
    ledcSetup(MOTOR_PWM_CHANNEL_B, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RESOLUTION);
    ledcAttachPin(PIN_MOTOR_PWMA, MOTOR_PWM_CHANNEL_A);
    ledcAttachPin(PIN_MOTOR_PWMB, MOTOR_PWM_CHANNEL_B);

    // STBY HIGH enables the driver; wheels stay stopped until commanded.
    digitalWrite(PIN_MOTOR_STBY, HIGH);
    stop();

    _initialized = true;
    Serial.println("[motor] TB6612 initialized (STOP)");
}

void MotorDriver::update() {
    if (_direction == MotorDirection::STOP) return;
    if (static_cast<int32_t>(millis() - _deadlineMs) >= 0) {
        stop();
        Serial.println("[motor] auto-stop (motion deadline reached)");
    }
}

bool MotorDriver::startMotion(MotorDirection dir, uint8_t speed, uint32_t durationMs) {
    if (!_initialized || dir == MotorDirection::STOP) {
        stop();
        return false;
    }

    speed    = clampSpeed(speed);
    durationMs = clampDuration(durationMs);

    _direction  = dir;
    _speed      = speed;
    _deadlineMs = millis() + durationMs;

    switch (dir) {
        case MotorDirection::FORWARD:
            setLeftWheel(true, speed);
            setRightWheel(true, speed);
            break;
        case MotorDirection::BACKWARD:
            setLeftWheel(false, speed);
            setRightWheel(false, speed);
            break;
        case MotorDirection::TURN_LEFT:
            setLeftWheel(false, speed);   // left wheel reverses
            setRightWheel(true, speed);   // right wheel forward
            break;
        case MotorDirection::TURN_RIGHT:
            setLeftWheel(true, speed);
            setRightWheel(false, speed);
            break;
        default:
            stop();
            return false;
    }

    Serial.printf("[motor] dir=%d speed=%u duration=%lums\n",
                  static_cast<int>(dir), speed, static_cast<unsigned long>(durationMs));
    return true;
}

void MotorDriver::stop() {
    _direction  = MotorDirection::STOP;
    _speed      = 0;
    _deadlineMs = 0;

    // Brake mode on both channels: IN1 = IN2 = HIGH, PWM = 0 (TB6612 short brake).
    digitalWrite(PIN_MOTOR_AIN1, HIGH);
    digitalWrite(PIN_MOTOR_AIN2, HIGH);
    digitalWrite(PIN_MOTOR_BIN1, HIGH);
    digitalWrite(PIN_MOTOR_BIN2, HIGH);
    writeDuty(MOTOR_PWM_CHANNEL_A, 0);
    writeDuty(MOTOR_PWM_CHANNEL_B, 0);
}

uint32_t MotorDriver::remainingMs() const {
    if (_direction == MotorDirection::STOP) return 0;
    uint32_t now = millis();
    return (_deadlineMs > now) ? (_deadlineMs - now) : 0;
}

// ------------------------------------------------------------
// Internals
// ------------------------------------------------------------

void MotorDriver::setLeftWheel(bool forward, uint8_t speed) {
    digitalWrite(PIN_MOTOR_AIN1, forward ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_AIN2, forward ? LOW  : HIGH);
    writeDuty(MOTOR_PWM_CHANNEL_A, speed);
}

void MotorDriver::setRightWheel(bool forward, uint8_t speed) {
    digitalWrite(PIN_MOTOR_BIN1, forward ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_BIN2, forward ? LOW  : HIGH);
    writeDuty(MOTOR_PWM_CHANNEL_B, speed);
}

void MotorDriver::writeDuty(uint8_t ledcChannel, uint8_t duty) {
    ledcWrite(ledcChannel, duty);
}

uint8_t MotorDriver::clampSpeed(uint8_t speed) {
    if (speed < MOTOR_SPEED_MIN)  return MOTOR_SPEED_MIN;
    if (speed > MOTOR_SPEED_MAX)  return MOTOR_SPEED_MAX;
    return speed;
}

uint32_t MotorDriver::clampDuration(uint32_t durationMs) {
    // Hard safety cap: no motion may outlive this window even if the
    // requested duration (or malformed input) asks for more.
    if (durationMs == 0)                  return MOVE_SMALL_MS;
    if (durationMs > MOTOR_WATCHDOG_MS)   return MOTOR_WATCHDOG_MS;
    return durationMs;
}
