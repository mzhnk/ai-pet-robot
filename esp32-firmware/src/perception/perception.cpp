/**
 * perception.cpp — sensor fusion + debounced event emission.
 *
 * All transitions are debounced (PERSON_CONFIRM_FRAMES, obstacle
 * hysteresis in the sonar driver, FALL_CONFIRM_MS in the IMU driver)
 * so behavior sees clean, deterministic perception changes.
 */
#include "perception.h"

#include <Arduino.h>

#include "../config.h"
#include "../distance/distance.h"
#include "../imu/imu.h"

PerceptionEngine perception;

namespace {

const char* motionToString(WorldMotion m) {
    switch (m) {
        case WorldMotion::STILL:  return "still";
        case WorldMotion::MOVING: return "moving";
        case WorldMotion::SHOCK:  return "shock";
    }
    return "unknown";
}

} // namespace

void PerceptionEngine::begin() {
    sonar.begin();
    imu().begin();          // ok if absent: robot stays functional without it

    visionLink.begin(visionTrampoline);

    for (uint8_t i = 0; i < EVENT_QUEUE_SIZE; ++i) _queue[i] = PerceptionEvent::NONE;
    Serial.println("[perception] ready");
}

void PerceptionEngine::visionTrampoline(const VisionFrame& frame) {
    perception.onVisionFrame(frame);
}

void PerceptionEngine::update() {
    sonar.update();
    imu().update();

    fuseSonar();
    fuseImu();
    fuseVisionOnline();
    supervisePersonPresence();
    superviseFallRecovery();

    world.lastUpdateMs = millis();
}

// ------------------------------------------------------------
// Fusion
// ------------------------------------------------------------

void PerceptionEngine::fuseSonar() {
    world.hasDistance = sonar.hasReading();
    if (world.hasDistance) {
        world.distanceCm    = sonar.distanceCm();
        world.obstacleClose = sonar.obstacleClose();
    } else {
        world.obstacleClose = false;
    }
    world.obstacle = sonar.obstacleActive() && world.hasDistance;

    if (world.obstacle != _prevObstacle) {
        _prevObstacle = world.obstacle;
        emit(world.obstacle ? PerceptionEvent::OBSTACLE_DETECTED
                            : PerceptionEvent::OBSTACLE_CLEARED);
    }
}

void PerceptionEngine::fuseImu() {
    world.imuHealthy = imu().healthy();
    if (!world.imuHealthy) {
        world.upright     = true;    // blind but optimistic: no false alarms
        world.inverted    = false;
        world.motion      = WorldMotion::STILL;
        return;
    }

    world.pitchDeg = imu().pitchDeg();
    world.rollDeg  = imu().rollDeg();

    const RobotOrientation orient = imu().orientation();
    world.upright  = (orient == RobotOrientation::UPRIGHT);
    world.inverted = (orient == RobotOrientation::INVERTED);

    switch (imu().motionLevel()) {
        case ImuMotionLevel::STILL:  world.motion = WorldMotion::STILL;  break;
        case ImuMotionLevel::MOVING: world.motion = WorldMotion::MOVING; break;
        case ImuMotionLevel::SHOCK:  world.motion = WorldMotion::SHOCK;  break;
    }
}

void PerceptionEngine::fuseVisionOnline() {
    world.visionOnline = visionLink.cameraOnline();
}

void PerceptionEngine::onVisionFrame(const VisionFrame& frame) {
    world.visionScore  = frame.score;
    world.personLastSeenMs = millis();

    if (frame.person) {
        if (frame.zone != TargetZone::NONE) world.targetZone = frame.zone;
        if (_personConfirmCount < 255) ++_personConfirmCount;
        if (!_prevPersonConfirmed &&
            _personConfirmCount >= PERSON_CONFIRM_FRAMES) {
            world.personPresent = true;
            _prevPersonConfirmed = true;
            emit(PerceptionEvent::PERSON_DETECTED);
        }
    } else {
        _personConfirmCount = 0;
        // personPresent is cleared by supervisePersonPresence() after the
        // PERSON_LOST_MS grace window, so brief occlusions do not flap.
    }
}

void PerceptionEngine::supervisePersonPresence() {
    if (!world.personPresent) return;
    if (!world.visionOnline) {
        // Link died: do not keep chasing a ghost.
        world.personPresent = false;
        _prevPersonConfirmed = false;
        _personConfirmCount = 0;
        emit(PerceptionEvent::PERSON_LOST);
        return;
    }
    if (millis() - world.personLastSeenMs > PERSON_LOST_MS) {
        world.personPresent = false;
        _prevPersonConfirmed = false;
        world.targetZone = TargetZone::NONE;
        emit(PerceptionEvent::PERSON_LOST);
    }
}

void PerceptionEngine::superviseFallRecovery() {
    const bool latched = imu().fallLatched();

    if (latched && !_prevFallLatched) {
        world.fallLatched = true;
        emit(PerceptionEvent::FALL_DETECTED);
    }

    if (world.fallLatched && !latched) {
        // IMU latch was cleared externally; mirror it.
        world.fallLatched = false;
    }

    if (world.fallLatched && world.upright && !world.inverted) {
        if (_uprightSinceMs == 0) {
            _uprightSinceMs = millis();
        } else if (millis() - _uprightSinceMs >= FAULT_RECOVER_MS) {
            imu().clearFallLatch();
            world.fallLatched = false;
            _uprightSinceMs = 0;
            emit(PerceptionEvent::ORIENTATION_RECOVERED);
        }
    } else {
        _uprightSinceMs = 0;
    }

    _prevFallLatched = imu().fallLatched();
}

// ------------------------------------------------------------
// Event queue
// ------------------------------------------------------------

void PerceptionEngine::emit(PerceptionEvent ev) {
    if (ev == PerceptionEvent::NONE) return;
    const uint8_t next = (_qHead + 1) % EVENT_QUEUE_SIZE;
    if (next == _qTail) return;   // queue full: drop newest (oldest kept)
    _queue[_qHead] = ev;
    _qHead = next;
    Serial.printf("[perception] event %d\n", static_cast<int>(ev));
}

bool PerceptionEngine::consumeEvent(PerceptionEvent& out) {
    if (_qHead == _qTail) return false;
    out = _queue[_qTail];
    _queue[_qTail] = PerceptionEvent::NONE;
    _qTail = (_qTail + 1) % EVENT_QUEUE_SIZE;
    return true;
}

const char* perceptionMotionName(const WorldState& w) {
    return motionToString(w.motion);
}
