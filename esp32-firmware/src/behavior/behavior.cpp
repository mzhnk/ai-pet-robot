/**
 * behavior.cpp — the robot's mind (V2).
 *
 * Every timing here is millis()-based; nothing blocks. The robot stays
 * expressive even when the network is down: idle personality, touch
 * reactions, perception-driven curiosity and obstacle avoidance all run
 * locally. Perception only suggests; safety limits are enforced here and
 * in the motor driver regardless of which state is active.
 */
#include "behavior.h"

#include "../config.h"
#include "../connection/connection.h"
#include "../display/display.h"
#include "../led/led.h"
#include "../motor/motor.h"
#include "../sound/sound.h"
#include "../touch/touch.h"

BehaviorController behavior;

namespace {

const char* behaviorStateToString(BehaviorState s) {
    switch (s) {
        case BehaviorState::BOOTING:        return "BOOTING";
        case BehaviorState::IDLE:           return "IDLE";
        case BehaviorState::EXPLORE:        return "EXPLORE";
        case BehaviorState::THINKING:       return "THINKING";
        case BehaviorState::HAPPY:          return "HAPPY";
        case BehaviorState::SLEEP:          return "SLEEP";
        case BehaviorState::CURIOUS:        return "CURIOUS";
        case BehaviorState::FOLLOW:         return "FOLLOW";
        case BehaviorState::AVOID_OBSTACLE: return "AVOID_OBSTACLE";
        case BehaviorState::SURPRISED:      return "SURPRISED";
        case BehaviorState::SEARCH:         return "SEARCH";
        case BehaviorState::INTERACT:       return "INTERACT";
        case BehaviorState::FAULT:          return "FAULT";
    }
    return "UNKNOWN";
}

uint32_t randomRange(uint32_t minMs, uint32_t maxMs) {
    if (maxMs <= minMs) return minMs;
    return minMs + random(maxMs - minMs);
}

constexpr uint16_t WAKE_BEEP_FREQ_HZ = 880;
constexpr uint16_t WAKE_BEEP_MS      = 80;

// States that mean "engaged with a person" for the V2 flow.
bool isPersonState(BehaviorState s) {
    switch (s) {
        case BehaviorState::CURIOUS:
        case BehaviorState::FOLLOW:
        case BehaviorState::SEARCH:
        case BehaviorState::INTERACT:
            return true;
        default:
            return false;
    }
}

} // namespace

// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------

void BehaviorController::begin() {
    _bootStartMs = millis();
    _lastInputMs = _bootStartMs;

    display.setFace(Face::NEUTRAL);
    sound.play(SoundId::STARTUP);
    leds.setMode(LedMode::PULSE);

    scheduleIdleTimers();
    _state = BehaviorState::BOOTING;
    Serial.println("[behavior] BOOTING");
}

void BehaviorController::update() {
    const uint32_t now = millis();

    // Touch is always alive, in every state (it is also the wake source).
    handleTouch();

    // Perception-driven transitions that do not need an explicit event.
    runPerceptionReactions();

    switch (_state) {
        case BehaviorState::BOOTING:
            if (now - _bootStartMs >= BOOT_SEQUENCE_MS) {
                enterIdle();
            }
            break;

        case BehaviorState::IDLE:
            runIdlePersonality();
            break;

        case BehaviorState::EXPLORE: {
            // Stepwise small motions, alternating direction; auto-expires to IDLE.
            const uint32_t stepWindow = EXPLORE_STEP_MS;
            const uint32_t elapsed = now - _stateEnteredExploreMs;
            const uint8_t step = static_cast<uint8_t>(elapsed / stepWindow);
            if (step != _exploreStep && step < EXPLORE_STEP_COUNT) {
                _exploreStep = step;
                const MotorDirection dir = (step % 2 == 0) ? MotorDirection::TURN_LEFT
                                                           : MotorDirection::TURN_RIGHT;
                motors.startMotion(dir, MOTOR_SPEED_MIN, EXPLORE_STEP_MS - 100);
            }
            if (now >= _stateEndMs) {
                enterIdle();
            }
            break;
        }

        case BehaviorState::THINKING:
            if (_awaitingAiResponse && now >= _stateEndMs) {
                // The Local AI did not answer in time; degrade gracefully.
                Serial.println("[behavior] AI response timeout -> IDLE");
                _awaitingAiResponse = false;
                sound.play(SoundId::ERROR);
                enterIdle();
            }
            break;

        case BehaviorState::HAPPY:
            if (now >= _stateEndMs) {
                enterIdle();
            }
            break;

        case BehaviorState::SLEEP:
            // Nothing but the touch wake; keep outputs dark.
            break;

        case BehaviorState::CURIOUS:
            if (now >= _stateEndMs) {
                if (world.personPresent &&
                    world.hasDistance && world.distanceCm <= INTERACT_DISTANCE_CM) {
                    enterInteract();
                } else if (world.personPresent && world.targetZone != TargetZone::NONE &&
                           now - _lastFollowEndMs >= FOLLOW_COOLDOWN_MS) {
                    enterFollow();
                } else {
                    enterIdle();
                }
            }
            break;

        case BehaviorState::FOLLOW:
            if (now >= _stateEndMs) {
                // Bounded chase: stop and cool down even if the person stays.
                _lastFollowEndMs = now;
                enterInteract();
                break;
            }
            if (now - _phaseStartMs >= FOLLOW_STEP_MS) {
                _phaseStartMs = now;
                if (world.targetZone == TargetZone::LEFT) {
                    motors.startMotion(MotorDirection::TURN_LEFT, MOTOR_SPEED_MIN,
                                       MOVE_TURN_MS);
                } else if (world.targetZone == TargetZone::RIGHT) {
                    motors.startMotion(MotorDirection::TURN_RIGHT, MOTOR_SPEED_MIN,
                                       MOVE_TURN_MS);
                } else if (world.targetZone == TargetZone::CENTER &&
                           world.hasDistance && !world.obstacle &&
                           world.distanceCm > FOLLOW_MIN_GAP_CM) {
                    motors.startMotion(MotorDirection::FORWARD, MOTOR_SPEED_MIN,
                                       MOVE_SMALL_MS);
                }
            }
            break;

        case BehaviorState::AVOID_OBSTACLE:
            runAvoidTimeline();
            break;

        case BehaviorState::SURPRISED:
            if (now >= _stateEndMs) {
                enterAvoid();
            }
            break;

        case BehaviorState::SEARCH:
            if (now >= _nextSearchLookMs) {
                _searchLookLeft = !_searchLookLeft;
                display.triggerAnimation(_searchLookLeft ? FaceAnim::LOOK_LEFT
                                                         : FaceAnim::LOOK_RIGHT);
                _nextSearchLookMs = now + AVOID_INSPECT_MS;
            }
            if (now >= _stateEndMs) {
                enterIdle();
            }
            break;

        case BehaviorState::INTERACT:
            if (now >= _stateEndMs) {
                if (world.personPresent) {
                    // Person still here: keep the engagement alive.
                    enterInteract();
                } else {
                    enterSearch();
                }
            }
            break;

        case BehaviorState::FAULT:
            // Recovery is detected in runPerceptionReactions().
            break;
    }

    display.update();
}

// ------------------------------------------------------------
// V2: perception-driven reactions
// ------------------------------------------------------------

void BehaviorController::runPerceptionReactions() {
    // 1) Fall dominates every state except boot and fault itself.
    if (_state != BehaviorState::FAULT && _state != BehaviorState::BOOTING &&
        world.fallLatched) {
        enterFault();
        return;
    }

    // 2) FAULT recovery: IMU latch cleared by perception after upright grace.
    if (_state == BehaviorState::FAULT && !world.fallLatched) {
        Serial.println("[behavior] recovered -> IDLE");
        enterIdle();
        sound.beep(WAKE_BEEP_FREQ_HZ, WAKE_BEEP_MS * 2);
        return;
    }

    if (_state == BehaviorState::FAULT) return;

    // 3) Emergency-close obstacle startles the pet out of most states.
    if (world.obstacleClose &&
        _state != BehaviorState::SURPRISED &&
        _state != BehaviorState::AVOID_OBSTACLE &&
        _state != BehaviorState::THINKING &&
        _state != BehaviorState::SLEEP &&
        _state != BehaviorState::BOOTING) {
        enterSurprised();
        return;
    }

    // 4) Sustained obstacle interrupts movement-heavy states.
    if (world.obstacle && _state != BehaviorState::AVOID_OBSTACLE &&
        _state != BehaviorState::SURPRISED &&
        _state != BehaviorState::SLEEP && _state != BehaviorState::BOOTING &&
        _state != BehaviorState::THINKING &&
        (motors.isMoving() || _state == BehaviorState::FOLLOW ||
         _state == BehaviorState::EXPLORE || _state == BehaviorState::IDLE)) {
        enterAvoid();
        return;
    }

    // 5) Presence keeps the pet awake while idling.
    if (_state == BehaviorState::IDLE && world.personPresent) {
        _sleepAtMs = millis() + randomRange(IDLE_TO_SLEEP_MIN_MS, IDLE_TO_SLEEP_MAX_MS);
    }
}

void BehaviorController::onPerceptionEvent(PerceptionEvent ev) {
    if (ev == PerceptionEvent::NONE) return;

    switch (ev) {
        case PerceptionEvent::PERSON_DETECTED:
            Serial.println("[behavior] person detected");
            if (_state == BehaviorState::IDLE || _state == BehaviorState::EXPLORE ||
                _state == BehaviorState::SLEEP || _state == BehaviorState::HAPPY) {
                enterCurious();
            } else if (isPersonState(_state) && _state != BehaviorState::CURIOUS) {
                // Re-found during SEARCH / reappeared during FOLLOW.
                enterCurious();
            }
            break;

        case PerceptionEvent::PERSON_LOST:
            Serial.println("[behavior] person lost");
            if (isPersonState(_state) && _state != BehaviorState::SEARCH) {
                enterSearch();
            }
            break;

        case PerceptionEvent::OBSTACLE_DETECTED:
            // Handled by runPerceptionReactions (checks context first);
            // fallback entry for quiet states.
            if (_state == BehaviorState::IDLE || _state == BehaviorState::EXPLORE ||
                isPersonState(_state)) {
                enterAvoid();
            }
            break;

        case PerceptionEvent::OBSTACLE_CLEARED:
            Serial.println("[behavior] obstacle cleared");
            break;

        case PerceptionEvent::FALL_DETECTED:
            enterFault();
            break;

        case PerceptionEvent::ORIENTATION_RECOVERED:
            // Recovery path lives in runPerceptionReactions (grace + beep).
            break;

        case PerceptionEvent::NONE:
        default:
            break;
    }
}

// ------------------------------------------------------------
// Touch handling (state-transitional input)
// ------------------------------------------------------------

void BehaviorController::handleTouch() {
    if (!touchSensor.consumeTouchEvent()) return;
    Serial.println("[behavior] touch detected");
    noteInput();

    switch (_state) {
        case BehaviorState::SLEEP:
            // Wake up: gentle beep, blink, back to IDLE.
            enterIdle();
            sound.beep(WAKE_BEEP_FREQ_HZ, WAKE_BEEP_MS);
            display.triggerAnimation(FaceAnim::BLINK);
            break;

        case BehaviorState::THINKING:
            // Already processing; ignore extra presses.
            break;

        case BehaviorState::BOOTING:
        case BehaviorState::FAULT:
            // Too early / too hurt to interact meaningfully; acknowledge only.
            sound.beep(WAKE_BEEP_FREQ_HZ, WAKE_BEEP_MS);
            break;

        default:  // IDLE / EXPLORE / HAPPY / V2 person states
            if (_online) {
                enterThinking();
                char buf[PROTOCOL_TX_BUFFER_SIZE];
                const size_t len = protocol_serialize_touch_head(buf, sizeof(buf));
                if (len > 0) {
                    connection.sendEvent(buf);
                }
            } else {
                // Offline: self-contained joy, no AI needed.
                Serial.println("[behavior] offline touch -> local HAPPY");
                enterHappy(true);
            }
            break;
    }
}

// ------------------------------------------------------------
// State entries
// ------------------------------------------------------------

void BehaviorController::enterIdle() {
    _state = BehaviorState::IDLE;
    Serial.println("[behavior] -> IDLE");

    _emotion = (_emotion == RobotEmotion::SLEEPY) ? RobotEmotion::NEUTRAL : _emotion;
    display.setFace(Face::NEUTRAL);
    if (leds.mode() == LedMode::OFF || leds.mode() == LedMode::THINKING ||
        leds.mode() == LedMode::ERROR) {
        leds.setMode(LedMode::PULSE);
    }

    motors.stop();
    scheduleIdleTimers();

    // Random sleep window inside the 30-60 s contract.
    _sleepAtMs = millis() + randomRange(IDLE_TO_SLEEP_MIN_MS, IDLE_TO_SLEEP_MAX_MS);

    announceBootIfNeeded();
}

void BehaviorController::announceBootIfNeeded() {
    // Send boot_completed once per boot, on the first link that is alive
    // after (or during) BOOTING — so the Local AI can greet the user.
    if (_bootAnnounced || !_online || _state == BehaviorState::BOOTING) return;
    _bootAnnounced = true;
    char buf[PROTOCOL_TX_BUFFER_SIZE];
    const size_t len = protocol_serialize_boot_completed(buf, sizeof(buf));
    if (len > 0) connection.sendEvent(buf);
    Serial.println("[behavior] boot_completed announced");
}

void BehaviorController::enterExplore() {
    _state = BehaviorState::EXPLORE;
    _exploreStep = 0;
    _stateEnteredExploreMs = millis();
    _stateEndMs  = millis() + (EXPLORE_STEP_MS * EXPLORE_STEP_COUNT);
    _emotion = RobotEmotion::CURIOUS;
    display.setFace(Face::CURIOUS);
    Serial.println("[behavior] -> EXPLORE");
}

void BehaviorController::enterThinking() {
    _state = BehaviorState::THINKING;
    _awaitingAiResponse = true;
    _stateEndMs = millis() + THINKING_TIMEOUT_MS;
    _emotion = RobotEmotion::THINKING;

    display.setFace(Face::THINKING);
    leds.setMode(LedMode::THINKING);
    sound.play(SoundId::THINKING);
    Serial.println("[behavior] -> THINKING (waiting for Local AI)");
}

void BehaviorController::enterHappy(bool withSound) {
    _state = BehaviorState::HAPPY;
    _stateEndMs = millis() + HAPPY_DURATION_MS;
    _emotion = RobotEmotion::HAPPY;

    display.setFace(Face::HAPPY);
    display.triggerAnimation(FaceAnim::SMILE);
    leds.setMode(LedMode::HAPPY);
    if (withSound) sound.play(SoundId::HAPPY);

    // Optional small celebratory movement (bounded by the motor safety cap).
    if (!world.obstacle) {
        motors.startMotion(MotorDirection::FORWARD, MOTOR_SPEED_MIN, MICRO_MOVE_MS);
    }
    Serial.println("[behavior] -> HAPPY");
}

void BehaviorController::stopAllOutput() {
    motors.stop();
    leds.setMode(LedMode::OFF);
    sound.stop();
}

void BehaviorController::enterSleep() {
    _state = BehaviorState::SLEEP;
    _emotion = RobotEmotion::SLEEPY;

    stopAllOutput();
    display.setFace(Face::SLEEP);
    Serial.println("[behavior] -> SLEEP");
}

// ------------------------------------------------------------
// V2 state entries
// ------------------------------------------------------------

void BehaviorController::enterCurious() {
    _state = BehaviorState::CURIOUS;
    _stateEndMs = millis() + CURIOUS_DURATION_MS;
    _emotion = RobotEmotion::CURIOUS;

    display.setFace(Face::CURIOUS);
    display.triggerAnimation(FaceAnim::TILT);
    leds.setMode(LedMode::PULSE);
    sound.beep(WAKE_BEEP_FREQ_HZ + 220, WAKE_BEEP_MS);

    // Orient toward the person once; FOLLOW refines afterwards.
    switch (world.targetZone) {
        case TargetZone::LEFT:
            motors.startMotion(MotorDirection::TURN_LEFT, MOTOR_SPEED_MIN, MOVE_TURN_MS);
            break;
        case TargetZone::RIGHT:
            motors.startMotion(MotorDirection::TURN_RIGHT, MOTOR_SPEED_MIN, MOVE_TURN_MS);
            break;
        default:
            display.triggerAnimation(FaceAnim::LOOK_LEFT);
            break;
    }
    Serial.println("[behavior] -> CURIOUS");
}

void BehaviorController::enterFollow() {
    _state = BehaviorState::FOLLOW;
    _stateEndMs = millis() + FOLLOW_MAX_MS;   // hard bound on any chase
    _phaseStartMs = millis();
    _emotion = RobotEmotion::CURIOUS;

    display.setFace(Face::CURIOUS);
    Serial.println("[behavior] -> FOLLOW");
}

void BehaviorController::enterAvoid() {
    _state = BehaviorState::AVOID_OBSTACLE;
    _avoidPhase = 0;
    _phaseStartMs = millis();

    motors.stop();
    _emotion = RobotEmotion::CURIOUS;
    display.setFace(Face::CURIOUS);
    display.triggerAnimation(FaceAnim::LOOK_LEFT);
    Serial.println("[behavior] -> AVOID_OBSTACLE");
}

void BehaviorController::enterSurprised() {
    _state = BehaviorState::SURPRISED;
    _stateEndMs = millis() + SURPRISED_DURATION_MS;
    _phaseStartMs = millis();
    _emotion = RobotEmotion::SCARED;

    motors.stop();
    if (world.hasDistance && !world.obstacleClose) {
        // Moderate surprise: small hop back (still bounded by the driver).
        motors.startMotion(MotorDirection::BACKWARD, MOTOR_SPEED_MIN, AVOID_BACKOFF_MS);
    }
    display.setFace(Face::SCARED);
    leds.setMode(LedMode::ERROR);
    sound.play(SoundId::ERROR);
    Serial.println("[behavior] -> SURPRISED");
}

void BehaviorController::enterSearch() {
    _state = BehaviorState::SEARCH;
    _stateEndMs = millis() + SEARCH_DURATION_MS;
    _nextSearchLookMs = millis();
    _searchLookLeft = false;
    _emotion = RobotEmotion::CURIOUS;

    motors.stop();
    display.setFace(Face::CURIOUS);
    Serial.println("[behavior] -> SEARCH");
}

void BehaviorController::enterInteract() {
    _state = BehaviorState::INTERACT;
    _stateEndMs = millis() + INTERACT_DURATION_MS;
    _emotion = RobotEmotion::HAPPY;

    display.setFace(Face::HAPPY);
    display.triggerAnimation(FaceAnim::SMILE);
    leds.setMode(LedMode::HAPPY);
    sound.play(SoundId::HAPPY);
    Serial.println("[behavior] -> INTERACT");
}

void BehaviorController::enterFault() {
    if (_state == BehaviorState::FAULT) return;   // already latched
    _state = BehaviorState::FAULT;
    _emotion = RobotEmotion::SCARED;

    stopAllOutput();                       // motors + LEDs + sound: safe
    display.setFace(Face::SCARED);
    display.setSpeech("Aduh!");
    leds.setMode(LedMode::ERROR);
    sound.play(SoundId::ERROR);
    Serial.println("[behavior] -> FAULT (fall/tilt latched)");
}

void BehaviorController::runAvoidTimeline() {
    const uint32_t elapsed = millis() - _phaseStartMs;

    switch (_avoidPhase) {
        case 0:   // inspect left (animation started at entry)
            if (elapsed >= AVOID_INSPECT_MS) {
                _avoidPhase = 1;
                _phaseStartMs = millis();
                display.triggerAnimation(FaceAnim::LOOK_RIGHT);
            }
            break;

        case 1:   // inspect right, then back off
            if (elapsed >= AVOID_INSPECT_MS) {
                _avoidPhase = 2;
                _phaseStartMs = millis();
                motors.startMotion(MotorDirection::BACKWARD, MOTOR_SPEED_MIN,
                                   AVOID_BACKOFF_MS);
            }
            break;

        case 2:   // turn away from the blocked heading
            if (elapsed >= AVOID_BACKOFF_MS + 60) {
                _avoidPhase = 3;
                _phaseStartMs = millis();
                motors.startMotion(MotorDirection::TURN_LEFT, MOTOR_SPEED_MIN,
                                   MOVE_TURN_MS * 2);
            }
            break;

        default:  // done
            if (!motors.isMoving()) {
                enterIdle();
            }
            break;
    }
}

// ------------------------------------------------------------
// Idle personality
// ------------------------------------------------------------

void BehaviorController::scheduleIdleTimers() {
    const uint32_t now = millis();
    _nextBlinkMs         = now + randomRange(IDLE_BLINK_MIN_MS, IDLE_BLINK_MAX_MS);
    _nextGlanceMs        = now + randomRange(IDLE_GLANCE_MIN_MS, IDLE_GLANCE_MAX_MS);
    _nextMicroMoveMs     = now + randomRange(IDLE_MICRO_MOVE_MIN_MS, IDLE_MICRO_MOVE_MAX_MS);
    _nextChirpMs         = now + randomRange(IDLE_CHIRP_MIN_MS, IDLE_CHIRP_MAX_MS);
    _nextExploreDecisionMs = now + randomRange(IDLE_EXPLORE_DECISION_MIN_MS,
                                               IDLE_EXPLORE_DECISION_MAX_MS);
}

void BehaviorController::runIdlePersonality() {
    const uint32_t now = millis();

    if (now >= _sleepAtMs) {
        enterSleep();
        return;
    }

    if (now >= _nextBlinkMs) {
        display.triggerAnimation(FaceAnim::BLINK);
        _nextBlinkMs = now + randomRange(IDLE_BLINK_MIN_MS, IDLE_BLINK_MAX_MS);
    }

    if (now >= _nextGlanceMs) {
        display.triggerAnimation(random(2) ? FaceAnim::LOOK_LEFT : FaceAnim::LOOK_RIGHT);
        _nextGlanceMs = now + randomRange(IDLE_GLANCE_MIN_MS, IDLE_GLANCE_MAX_MS);
    }

    if (now >= _nextMicroMoveMs) {
        // Tiny alternating wiggle — alive but desktop-safe; never into obstacles.
        if (!world.obstacle) {
            const MotorDirection dir = random(2) ? MotorDirection::TURN_LEFT
                                                 : MotorDirection::TURN_RIGHT;
            motors.startMotion(dir, MOTOR_SPEED_MIN, MICRO_MOVE_MS);
        }
        _nextMicroMoveMs = now + randomRange(IDLE_MICRO_MOVE_MIN_MS, IDLE_MICRO_MOVE_MAX_MS);
    }

    if (now >= _nextChirpMs) {
        sound.beep(WAKE_BEEP_FREQ_HZ, WAKE_BEEP_MS);
        _nextChirpMs = now + randomRange(IDLE_CHIRP_MIN_MS, IDLE_CHIRP_MAX_MS);
    }

    if (now >= _nextExploreDecisionMs) {
        // Sometimes curiosity wins; sometimes it does not. Never explore
        // straight into a known obstacle.
        if (!world.obstacle && random(100) < 40) {
            enterExplore();
            return;
        }
        _nextExploreDecisionMs = now + randomRange(IDLE_EXPLORE_DECISION_MIN_MS,
                                                   IDLE_EXPLORE_DECISION_MAX_MS);
    }
}

// ------------------------------------------------------------
// Commands from the Local AI
// ------------------------------------------------------------

void BehaviorController::applyCommand(const RobotCommand& cmd) {
    Serial.printf("[behavior] AI command: emotion=%s anim=%s move=%s speech=%s\n",
                  emotion_to_string(cmd.emotion),
                  animation_to_string(cmd.animation),
                  movement_to_string(cmd.movement),
                  cmd.hasSpeech ? cmd.speech : "-");

    noteInput();

    // Any explicit AI command wakes the pet from sleep.
    if (_state == BehaviorState::SLEEP) {
        enterIdle();
    }

    const bool faultLocked = (_state == BehaviorState::FAULT);

    // 1) Emotion face (works even in FAULT — the face is safe)
    _emotion = cmd.emotion;
    switch (cmd.emotion) {
        case RobotEmotion::HAPPY:    display.setFace(Face::HAPPY);    break;
        case RobotEmotion::CURIOUS:  display.setFace(Face::CURIOUS);  break;
        case RobotEmotion::SLEEPY:   display.setFace(Face::SLEEPY);   break;
        case RobotEmotion::THINKING: display.setFace(Face::THINKING); break;
        case RobotEmotion::SCARED:   display.setFace(Face::SCARED);   break;
        case RobotEmotion::NEUTRAL:
        default:                     display.setFace(Face::NEUTRAL);  break;
    }

    // 2) One-shot animation
    if (cmd.hasAnimation) {
        FaceAnim anim = FaceAnim::NONE;
        switch (cmd.animation) {
            case RobotAnimation::SMILE:      anim = FaceAnim::SMILE;      break;
            case RobotAnimation::BLINK:      anim = FaceAnim::BLINK;      break;
            case RobotAnimation::LOOK_LEFT:  anim = FaceAnim::LOOK_LEFT;  break;
            case RobotAnimation::LOOK_RIGHT: anim = FaceAnim::LOOK_RIGHT; break;
            case RobotAnimation::TILT:       anim = FaceAnim::TILT;       break;
            case RobotAnimation::NONE:
            default: break;
        }
        if (anim != FaceAnim::NONE) display.triggerAnimation(anim);
    }

    // 3) Movement — deterministic safety: the AI can never command motion
    //    into a known obstacle, and cannot move a fallen robot at all.
    if (cmd.hasMovement) {
        if (faultLocked) {
            Serial.println("[behavior] movement ignored (FAULT lock)");
        } else {
            executeMovement(cmd.movement, cmd.speed, cmd.durationMs);
        }
    }

    // 4) Speech bubble
    if (cmd.hasSpeech) {
        display.setSpeech(cmd.speech);
    }

    // 5) Emotion-flavored effects + state transition out of THINKING
    if (_state == BehaviorState::THINKING) {
        _awaitingAiResponse = false;
        if (cmd.emotion == RobotEmotion::HAPPY) {
            leds.setMode(LedMode::HAPPY);
            sound.play(SoundId::HAPPY);
            _state = BehaviorState::HAPPY;
            _stateEndMs = millis() + HAPPY_DURATION_MS;
            Serial.println("[behavior] THINKING -> HAPPY (AI answered)");
        } else {
            leds.setMode(LedMode::PULSE);
            _state = BehaviorState::IDLE;
            scheduleIdleTimers();
            _sleepAtMs = millis() + randomRange(IDLE_TO_SLEEP_MIN_MS, IDLE_TO_SLEEP_MAX_MS);
            Serial.println("[behavior] THINKING -> IDLE (AI answered)");
        }
    } else if (!faultLocked) {
        switch (cmd.emotion) {
            case RobotEmotion::HAPPY:
                leds.setMode(LedMode::HAPPY);
                _state = BehaviorState::HAPPY;
                _stateEndMs = millis() + HAPPY_DURATION_MS;
                break;
            case RobotEmotion::SCARED:
                leds.setMode(LedMode::ERROR);
                break;
            case RobotEmotion::SLEEPY:
                leds.setMode(LedMode::PULSE);
                break;
            case RobotEmotion::THINKING:
                leds.setMode(LedMode::THINKING);
                break;
            default:
                leds.setMode(LedMode::PULSE);
                break;
        }
    }
}

void BehaviorController::executeMovement(RobotMovement move, uint8_t speedOverride,
                                         uint32_t durationOverrideMs) {
    // Dashboard/AI may fine-tune speed and duration; the driver clamps anyway.
    const uint8_t  speed    = (speedOverride > 0) ? speedOverride : MOTOR_SPEED_DEFAULT;
    switch (move) {
        case RobotMovement::FORWARD_SMALL:
            if (world.obstacle) {
                // Deterministic guard: never drive forward into a known obstacle.
                Serial.println("[behavior] forward blocked by obstacle -> STOP");
                motors.stop();
                break;
            }
            motors.startMotion(MotorDirection::FORWARD, speed,
                               durationOverrideMs ? durationOverrideMs : MOVE_SMALL_MS);
            break;
        case RobotMovement::BACKWARD_SMALL:
            motors.startMotion(MotorDirection::BACKWARD, speed,
                               durationOverrideMs ? durationOverrideMs : MOVE_SMALL_MS);
            break;
        case RobotMovement::TURN_LEFT:
            motors.startMotion(MotorDirection::TURN_LEFT, speed,
                               durationOverrideMs ? durationOverrideMs : MOVE_TURN_MS);
            break;
        case RobotMovement::TURN_RIGHT:
            motors.startMotion(MotorDirection::TURN_RIGHT, speed,
                               durationOverrideMs ? durationOverrideMs : MOVE_TURN_MS);
            break;
        case RobotMovement::STOP:
        default:
            motors.stop();
            break;
    }
}

// ------------------------------------------------------------
// Connection coupling
// ------------------------------------------------------------

void BehaviorController::onConnectionChanged(bool online, const char* reason) {
    _online = online;
    display.setLink(online);

    if (!online) {
        // Safety: never let motors run into a lost link.
        motors.stop();
        if (_state == BehaviorState::THINKING) {
            _awaitingAiResponse = false;
            enterIdle();
        }
        Serial.printf("[behavior] link down (%s) -> outputs safe\n", reason);
    } else {
        Serial.println("[behavior] link up");
        announceBootIfNeeded();
    }
}

// ------------------------------------------------------------
// Status provider (telemetry for the dashboard)
// ------------------------------------------------------------

void BehaviorController::fillStatus(RobotStatusFields& fields) {
    fields.state   = behaviorStateToString(_state);
    fields.emotion = emotion_to_string(_emotion);
    fields.uptimeS = millis() / 1000;
    fields.rssiDbm = connection.rssi();
    fields.wifiConnected = connection.wifiConnected();
    fields.wsConnected   = connection.isOnline();
}

const char* BehaviorController::stateName() const {
    return behaviorStateToString(_state);
}
