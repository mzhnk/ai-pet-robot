/**
 * perception.h — fusion layer between sensors and behavior.
 *
 * Pipeline (per the V2 blueprint):
 *   sensors (sonar / IMU / vision link)
 *     -> PerceptionEngine (sample, filter, debounce, hysteresis)
 *     -> WorldState (shared belief)
 *     -> events -> Behavior / Local AI
 *
 * No sensor drives behavior directly; behavior only consumes WorldState
 * plus the debounced PerceptionEvents emitted here.
 */
#pragma once

#include "../camera/camera.h"
#include "../world/world_state.h"

enum class PerceptionEvent : uint8_t {
    NONE = 0,
    PERSON_DETECTED,
    PERSON_LOST,
    OBSTACLE_DETECTED,
    OBSTACLE_CLEARED,
    FALL_DETECTED,
    ORIENTATION_RECOVERED,   // fall latch cleared: upright again
};

class PerceptionEngine {
public:
    void begin();
    void update();   // call every loop: samples sensors on their own cadences

    /// Pop one pending event (FIFO). Returns false when the queue is empty.
    bool consumeEvent(PerceptionEvent& out);

    const WorldState& state() const { return world; }

private:
    void onVisionFrame(const VisionFrame& frame);
    void fuseSonar();
    void fuseImu();
    void fuseVisionOnline();
    void supervisePersonPresence();
    void superviseFallRecovery();
    void emit(PerceptionEvent ev);

    static void visionTrampoline(const VisionFrame& frame);

    static constexpr uint8_t EVENT_QUEUE_SIZE = 8;
    PerceptionEvent _queue[EVENT_QUEUE_SIZE];
    uint8_t _qHead = 0;
    uint8_t _qTail = 0;

    uint8_t  _personConfirmCount = 0;
    bool     _prevPersonConfirmed = false;
    bool     _prevObstacle   = false;
    bool     _prevFallLatched = false;
    uint32_t _uprightSinceMs = 0;
};

extern PerceptionEngine perception;

/// Motion level as a wire-friendly string ("still"/"moving"/"shock").
const char* perceptionMotionName(const WorldState& w);
