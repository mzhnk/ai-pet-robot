/**
 * behavior.h — Behavior FSM (V2): reactive pet -> perceptive pet.
 *
 * V1 states (BOOTING/IDLE/EXPLORE/THINKING/HAPPY/SLEEP) and their
 * transitions are preserved. V2 adds perception-driven states:
 *   CURIOUS        person appeared -> orient toward them
 *   INTERACT       person near -> happy engagement
 *   FOLLOW         person present + target -> paced chasing
 *   SEARCH         lost the person -> look around
 *   AVOID_OBSTACLE obstacle -> stop, inspect, back off, turn away
 *   SURPRISED      sudden close obstacle -> startled hop
 *   FAULT          fall/tilt latched -> motors dead until recovery
 *
 * Behavior reads the shared WorldState and consumes PerceptionEvents;
 * it never talks to sensors directly. The Connection FSM stays separate.
 */
#pragma once

#include <Arduino.h>

#include "../perception/perception.h"
#include "../protocol/protocol.h"

enum class BehaviorState : uint8_t {
    BOOTING = 0,
    IDLE,
    EXPLORE,
    THINKING,
    HAPPY,
    SLEEP,
    // V2
    CURIOUS,
    FOLLOW,
    AVOID_OBSTACLE,
    SURPRISED,
    SEARCH,
    INTERACT,
    FAULT,
};

class BehaviorController {
public:
    void begin();
    void update();

    /// Apply a validated command from the Local AI (protocol layer already vetted it).
    void applyCommand(const RobotCommand& cmd);

    /// Called by main when the connection FSM changes state (safety + UI).
    void onConnectionChanged(bool online, const char* reason);

    /// V2: perception-driven inputs (called by main for each drained event).
    void onPerceptionEvent(PerceptionEvent ev);

    BehaviorState state() const { return _state; }
    RobotEmotion  currentEmotion() const { return _emotion; }
    const char*   stateName() const;
    bool          isOnline() const { return _online; }

    /// Provider for the connection layer's status heartbeat.
    /// (main.cpp wires it through a small static trampoline.)
    void fillStatus(RobotStatusFields& fields);

private:
    // V1 state entries
    void enterIdle();
    void enterExplore();
    void enterThinking();
    void enterHappy(bool withSound);
    void enterSleep();

    // V2 state entries
    void enterCurious();
    void enterFollow();
    void enterAvoid();
    void enterSurprised();
    void enterSearch();
    void enterInteract();
    void enterFault();

    // Personality helpers
    void handleTouch();
    void scheduleIdleTimers();
    void runIdlePersonality();
    void noteInput() { _lastInputMs = millis(); }

    void executeMovement(RobotMovement move, uint8_t speedOverride = 0,
                         uint32_t durationOverrideMs = 0);
    void stopAllOutput();
    void announceBootIfNeeded();
    void runPerceptionReactions();
    void runAvoidTimeline();

    BehaviorState _state = BehaviorState::BOOTING;
    RobotEmotion  _emotion = RobotEmotion::NEUTRAL;
    bool          _online = false;

    // BOOTING
    uint32_t _bootStartMs = 0;
    bool     _bootAnnounced = false;   // boot_completed sent to Local AI

    // State deadlines
    uint32_t _stateEndMs = 0;

    // IDLE personality timers
    uint32_t _nextBlinkMs = 0;
    uint32_t _nextGlanceMs = 0;
    uint32_t _nextMicroMoveMs = 0;
    uint32_t _nextChirpMs = 0;
    uint32_t _nextExploreDecisionMs = 0;
    uint32_t _lastInputMs = 0;
    uint32_t _sleepAtMs = 0;

    // EXPLORE stepping
    uint8_t  _exploreStep = 0;
    uint32_t _stateEnteredExploreMs = 0;

    // THINKING
    bool _awaitingAiResponse = false;

    // V2: perception-driven bookkeeping
    uint32_t _lastFollowEndMs = 0;      // FOLLOW_COOLDOWN_MS gate
    uint32_t _phaseStartMs = 0;         // AVOID/SURPRISED sub-timeline
    uint8_t  _avoidPhase = 0;
    bool     _searchLookLeft = false;
    uint32_t _nextSearchLookMs = 0;
};

extern BehaviorController behavior;
