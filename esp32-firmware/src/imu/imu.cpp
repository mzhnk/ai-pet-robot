/**
 * imu.cpp — MPU-6050 driver over raw I2C registers (no external library).
 *
 * Kept dependency-free on purpose: register access is tiny, well-known and
 * leaves the exact-sensor choice isolated behind ImuSensor/imu().
 *
 * Tilt conventions (degrees):
 *   pitch = atan2(-ax, sqrt(ay² + az²)), roll = atan2(ay, az)
 *   |tilt| <= UPRIGHT_MAX -> UPRIGHT; >= INVERTED -> INVERTED; else TILTED.
 * A fall latches when tilt exceeds ORIENTATION_TILT_MIN_DEG for
 * FALL_CONFIRM_MS, or instantly when clearly inverted.
 */
#include "imu.h"

#include <math.h>
#include <Wire.h>

#include "../config.h"

namespace {

constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_GYRO_CONFIG  = 0x1B;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_WHO_AM_I     = 0x75;

// Known WHO_AM_I responses (0x68 = genuine MPU-6050, others = compatible
// clones such as MPU-6500 variants sharing the same register map).
bool knownWhoAmI(uint8_t id) {
    switch (id) {
        case 0x68: case 0x70: case 0x71: case 0x73: case 0x98:
            return true;
        default:
            return false;
    }
}

const float RAD_TO_DEG_F = 57.29577951f;

float tiltMagnitude(float pitchDeg, float rollDeg) {
    return sqrtf(pitchDeg * pitchDeg + rollDeg * rollDeg);
}

} // namespace

// The only hardware-specific binding point.
static Mpu6050Sensor g_mpu6050;
ImuSensor& imu() { return g_mpu6050; }

bool Mpu6050Sensor::begin() {
    if (_healthy) return true;

    Wire.beginTransmission(IMU_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[imu] MPU-6050 not responding");
        return false;
    }

    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(REG_WHO_AM_I);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(IMU_I2C_ADDR, 1);
    if (!Wire.available()) return false;
    if (!knownWhoAmI(Wire.read())) {
        Serial.println("[imu] unexpected WHO_AM_I; continuing anyway");
    }

    // Wake up + select ranges. +-4g / +-500 dps match config.h scaling.
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(REG_PWR_MGMT_1);
    Wire.write(0x00);                       // clear sleep
    Wire.endTransmission();

    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(REG_GYRO_CONFIG);
    Wire.write(0x08);                        // +-500 dps
    Wire.endTransmission();

    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(REG_ACCEL_CONFIG);
    Wire.write(0x08);                        // +-4g
    Wire.endTransmission();

    _healthy = true;
    _tiltOngoing = false;
    _fallLatched = false;
    Serial.println("[imu] MPU-6050 ready");
    return true;
}

bool Mpu6050Sensor::readRaw(int16_t& ax, int16_t& ay, int16_t& az,
                            int16_t& gx, int16_t& gy, int16_t& gz) {
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom(IMU_I2C_ADDR, 14) != 14) return false;
    auto rd = []() -> int16_t {
        const uint8_t hi = Wire.read();
        const uint8_t lo = Wire.read();
        return static_cast<int16_t>((hi << 8) | lo);
    };
    ax = rd(); ay = rd(); az = rd();
    /* temp */ (void)rd();
    gx = rd(); gy = rd(); gz = rd();
    return true;
}

void Mpu6050Sensor::update() {
    if (!_healthy) {
        // Retry occasionally so a hot-plugged sensor can recover.
        const uint32_t now = millis();
        if (now - _lastSampleMs < 1000) return;
        _lastSampleMs = now;
        begin();
        return;
    }

    const uint32_t now = millis();
    if (now - _lastSampleMs < IMU_SAMPLE_MS) return;
    _lastSampleMs = now;

    int16_t ax, ay, az, gx, gy, gz;
    if (!readRaw(ax, ay, az, gx, gy, gz)) {
        _healthy = false;
        return;
    }
    _lastSampleOkMs = now;

    // +-4g -> 8192 LSB/g ; +-500 dps -> 65.5 LSB/dps
    const float axG = ax / 8192.0f;
    const float ayG = ay / 8192.0f;
    const float azG = az / 8192.0f;
    const float gxDps = gx / 65.5f;
    const float gyDps = gy / 65.5f;
    const float gzDps = gz / 65.5f;

    updateAngles(axG, ayG, azG);
    classify(axG, ayG, azG, gxDps, gyDps, gzDps);
}

void Mpu6050Sensor::updateAngles(float axG, float ayG, float azG) {
    const float pitch = atan2f(-axG, sqrtf(ayG * ayG + azG * azG)) * RAD_TO_DEG_F;
    const float roll  = atan2f(ayG, azG) * RAD_TO_DEG_F;

    const float alpha = IMU_ANGLE_EMA_PCT / 100.0f;
    _pitchDeg += alpha * (pitch - _pitchDeg);
    _rollDeg  += alpha * (roll  - _rollDeg);
}

void Mpu6050Sensor::classify(float axG, float ayG, float azG,
                             float gxDps, float gyDps, float gzDps) {
    const float accelG = sqrtf(axG * axG + ayG * ayG + azG * azG);
    const float gyroDps = sqrtf(gxDps * gxDps + gyDps * gyDps + gzDps * gzDps);

    _movingLastSample = gyroDps > MOTION_STILL_MAX_DPS;
    _shockLastSample  = accelG > IMU_SHOCK_G;

    // Fall latch: sustained heavy tilt, or an instant inversion.
    const float tilt = tiltMagnitude(_pitchDeg, _rollDeg);
    if (tilt >= ORIENTATION_INVERTED_DEG) {
        _fallLatched = true;
    } else if (tilt >= ORIENTATION_TILT_MIN_DEG) {
        if (!_tiltOngoing) {
            _tiltOngoing = true;
            _tiltSinceMs = millis();
        } else if (millis() - _tiltSinceMs >= FALL_CONFIRM_MS) {
            _fallLatched = true;
        }
    } else {
        _tiltOngoing = false;
    }
}

RobotOrientation Mpu6050Sensor::orientation() const {
    const float tilt = tiltMagnitude(_pitchDeg, _rollDeg);
    if (tilt >= ORIENTATION_INVERTED_DEG) return RobotOrientation::INVERTED;
    if (tilt <= ORIENTATION_UPRIGHT_MAX_DEG) return RobotOrientation::UPRIGHT;
    return RobotOrientation::TILTED;
}

ImuMotionLevel Mpu6050Sensor::motionLevel() const {
    // Sharp spikes are detected at classification time via accel magnitude;
    // here we expose the steady-state view derived from the latest sample.
    return _shockLastSample ? ImuMotionLevel::SHOCK : _movingLastSample
                                                     ? ImuMotionLevel::MOVING
                                                     : ImuMotionLevel::STILL;
}
