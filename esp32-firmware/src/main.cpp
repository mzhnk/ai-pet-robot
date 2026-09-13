/**
 * main.cpp — orchestrator only (V2).
 *
 * Wires the modules together and keeps the loop tight and non-blocking:
 *   connection -> vision link -> perception -> safety guards ->
 *   behavior -> hardware. All logic lives in the modules.
 *
 * V2 pipeline:
 *   sensors -> perception -> WorldState -> behavior -> motors/OLED/LED/sound
 *                                            -> Local AI (events + world_state)
 */
#include <Arduino.h>

#include "behavior/behavior.h"
#include "camera/camera.h"
#include "config.h"
#include "connection/connection.h"
#include "display/display.h"
#include "led/led.h"
#include "motor/motor.h"
#include "perception/perception.h"
#include "protocol/protocol.h"
#include "sound/sound.h"
#include "touch/touch.h"
#include "world/world_state.h"

namespace {

/// Local AI -> ESP32: parse + apply. Malformed input is logged and dropped.
void onCommandFromAi(const char* payload, size_t length) {
    RobotCommand cmd;
    if (protocol_parse_robot_command(payload, length, cmd)) {
        behavior.applyCommand(cmd);
    } else {
        Serial.printf("[main] ignored invalid AI payload (%u bytes)\n",
                      static_cast<unsigned>(length));
    }
}

/// Connection FSM -> behavior: safety hooks + link indicator.
void onConnectionState(ConnectionState newState, const char* reason) {
    behavior.onConnectionChanged(newState == ConnectionState::CONNECTED, reason);
}

/// Trampoline: the connection layer needs a plain function pointer, but
/// the status data lives in the behavior instance.
void statusProviderTrampoline(RobotStatusFields& fields) {
    behavior.fillStatus(fields);
}

/// Deterministic safety guards that outrank every FSM: a fall or a close
/// obstacle stops the motors immediately, before behavior even runs.
void applySafetyGuards() {
    if (world.fallLatched && motors.isMoving()) {
        Serial.println("[main] SAFETY: fall latched -> stop motors");
        motors.stop();
    }
    if (world.obstacle && motors.direction() == MotorDirection::FORWARD) {
        Serial.println("[main] SAFETY: obstacle ahead -> stop motors");
        motors.stop();
    }
}

/// Drain perception events into behavior + the Local AI link.
void dispatchPerceptionEvents() {
    PerceptionEvent ev;
    while (perception.consumeEvent(ev)) {
        behavior.onPerceptionEvent(ev);

        char buf[PROTOCOL_TX_BUFFER_SIZE];
        size_t len = 0;
        switch (ev) {
            case PerceptionEvent::PERSON_DETECTED:
                len = protocol_serialize_person_detected(buf, sizeof(buf),
                                                         world.targetZone);
                break;
            case PerceptionEvent::PERSON_LOST:
                len = protocol_serialize_person_lost(buf, sizeof(buf));
                break;
            case PerceptionEvent::OBSTACLE_DETECTED:
                len = protocol_serialize_obstacle_detected(buf, sizeof(buf));
                break;
            case PerceptionEvent::OBSTACLE_CLEARED:
                len = protocol_serialize_obstacle_cleared(buf, sizeof(buf));
                break;
            case PerceptionEvent::FALL_DETECTED:
                len = protocol_serialize_fall_detected(buf, sizeof(buf));
                break;
            default:
                break;   // ORIENTATION_RECOVERED is internal-only
        }
        if (len > 0) connection.sendEvent(buf);
    }
}

/// Periodic world_state summary so the Local AI/dashboard stays current
/// even without discrete events.
void broadcastWorldStateIfNeeded() {
    static uint32_t lastBroadcastMs = 0;
    const uint32_t now = millis();
    if (now - lastBroadcastMs < WORLD_STATE_BROADCAST_MS) return;
    lastBroadcastMs = now;

    WorldStateFields f;
    f.personPresent = world.personPresent;
    f.zone          = world.targetZone;
    f.hasDistance   = world.hasDistance;
    f.distanceCm    = world.distanceCm;
    f.obstacle      = world.obstacle;
    f.obstacleClose = world.obstacleClose;
    f.upright       = world.upright;
    f.inverted      = world.inverted;
    f.motion        = perceptionMotionName(world);
    f.behaviorState = behavior.stateName();
    f.visionOnline  = world.visionOnline;

    char buf[PROTOCOL_TX_BUFFER_SIZE];
    const size_t len = protocol_serialize_world_state(buf, sizeof(buf), f);
    if (len > 0) connection.sendEvent(buf);
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(100);   // boot-time only: let the USB CDC settle
    Serial.printf("\n[%s v%s] booting...\n", ROBOT_NAME, FIRMWARE_VERSION);

    // Hardware modules
    motors.begin();
    display.begin();
    sound.begin();
    leds.begin();
    touchSensor.begin();

    // Perception stack (sonar + IMU + vision link; graceful if sensors absent)
    perception.begin();

    // Connection + behavior (FSMs stay separate; main is the glue)
    connection.begin(onCommandFromAi, onConnectionState);
    connection.setStatusProvider(statusProviderTrampoline);
    behavior.begin();

    Serial.println("[main] setup complete");
}

void loop() {
    connection.update();
    visionLink.update();
    perception.update();

    applySafetyGuards();
    dispatchPerceptionEvents();

    behavior.update();
    motors.update();
    leds.update();
    sound.update();

    broadcastWorldStateIfNeeded();
}
