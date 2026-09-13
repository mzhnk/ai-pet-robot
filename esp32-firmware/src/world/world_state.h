/**
 * world_state.h — the robot's unified belief about its surroundings.
 *
 * Sensors never talk to behavior directly; perception fuses them into this
 * single WorldState. Anything that wants to know "what is happening" reads
 * this struct. Plain data, host-testable, no hardware dependencies.
 */
#pragma once

#include <stdint.h>

#include "../protocol/protocol.h"   // TargetZone

enum class WorldMotion : uint8_t {
    STILL = 0,
    MOVING,
    SHOCK,
};

struct WorldState {
    // Vision
    bool      personPresent    = false;
    TargetZone targetZone      = TargetZone::NONE;
    uint8_t   visionScore      = 0;      // last motion energy 0..255
    uint32_t  personLastSeenMs = 0;
    bool      visionOnline     = false;  // camera unit linked and fresh

    // Distance
    bool      hasDistance      = false;
    float     distanceCm       = 0.0f;
    bool      obstacle         = false;  // hysteresis-applied
    bool      obstacleClose    = false;  // inside the emergency threshold

    // IMU
    bool      imuHealthy       = false;
    bool      upright          = true;
    bool      inverted         = false;
    float     pitchDeg         = 0.0f;
    float     rollDeg          = 0.0f;
    WorldMotion motion         = WorldMotion::STILL;
    bool      fallLatched      = false;

    uint32_t  lastUpdateMs     = 0;
};

extern WorldState world;
