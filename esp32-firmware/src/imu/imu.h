/**
 * imu.h — isolated IMU abstraction.
 *
 * Perception only talks to ImuSensor; the concrete device lives behind a
 * factory in imu.cpp, so swapping hardware (e.g. MPU-6050 -> MPU-6886 or
 * an ICM-series) touches exactly one file and no call sites.
 *
 * Semantics provided to consumers:
 *  - pitch/roll (degrees, accel-derived + EMA smoothing)
 *  - orientation class: UPRIGHT / TILTED / INVERTED
 *  - motion level: STILL / MOVING / SHOCK (gyro magnitude + accel spike)
 *  - fall latch: set when a fall is confirmed; must be cleared explicitly
 */
#pragma once

#include <Arduino.h>

enum class RobotOrientation : uint8_t {
    UPRIGHT = 0,
    TILTED,      // leaning but not fallen
    INVERTED,    // upside down / fallen over
};

enum class ImuMotionLevel : uint8_t {
    STILL = 0,
    MOVING,
    SHOCK,       // sharp acceleration spike (bump / drop / kick)
};

class ImuSensor {
public:
    virtual ~ImuSensor() = default;

    /// Returns false when the device is absent/unresponsive (robot stays
    /// functional; perception just marks IMU as unhealthy).
    virtual bool begin() = 0;
    virtual void update() = 0;

    virtual bool healthy() const = 0;
    virtual float pitchDeg() const = 0;
    virtual float rollDeg() const = 0;
    virtual RobotOrientation orientation() const = 0;
    virtual ImuMotionLevel motionLevel() const = 0;

    /// Latched fall flag (stays true until clearFallLatch()).
    virtual bool fallLatched() const = 0;
    virtual void clearFallLatch() = 0;
};

/// Concrete implementation selected here (the ONLY place that knows the chip).
ImuSensor& imu();

class Mpu6050Sensor : public ImuSensor {
public:
    bool begin() override;
    void update() override;

    bool healthy() const override { return _healthy; }
    float pitchDeg() const override { return _pitchDeg; }
    float rollDeg() const override { return _rollDeg; }
    RobotOrientation orientation() const override;
    ImuMotionLevel motionLevel() const override;

    bool fallLatched() const override { return _fallLatched; }
    void clearFallLatch() override { _fallLatched = false; }

private:
    bool readRaw(int16_t& ax, int16_t& ay, int16_t& az,
                 int16_t& gx, int16_t& gy, int16_t& gz);
    void updateAngles(float axG, float ayG, float azG);
    void classify(float axG, float ayG, float azG,
                  float gxDps, float gyDps, float gzDps);

    bool     _healthy     = false;
    float    _pitchDeg    = 0.0f;
    float    _rollDeg     = 0.0f;
    bool     _movingLastSample = false;
    bool     _shockLastSample  = false;
    uint32_t _lastSampleMs = 0;
    uint32_t _lastSampleOkMs = 0;
    uint32_t _tiltSinceMs  = 0;      // sustained-tilt watchdog
    bool     _tiltOngoing  = false;
    bool     _fallLatched  = false;
};
