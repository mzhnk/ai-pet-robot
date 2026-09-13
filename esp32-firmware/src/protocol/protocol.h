/**
 * protocol.h — centralized WebSocket JSON protocol (ESP32 <-> Local AI).
 *
 * This module is intentionally free of Arduino.h dependencies so it can
 * be unit-tested natively (host) with PlatformIO [env:native].
 *
 * ESP32 -> Local AI events:
 *   {"event":"touch_head"}
 *   {"event":"boot_completed"}
 *   {"event":"obstacle_detected"}
 *   {"event":"heartbeat", "uptime_s":N, "rssi":N, "state":"...", "emotion":"..."}
 *
 * Local AI -> ESP32 command:
 *   {"emotion":"happy", "animation":"smile",
 *    "movement":"forward_small", "speech":"Halo!"}
 *
 * All parsing is defensive: malformed/unknown input returns false and the
 * caller simply logs + ignores it. Bad JSON can never crash the robot.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------
// Protocol buffer sizes (protocol-level constants; host-testable,
// therefore defined here instead of config.h which needs Arduino.h)
// ------------------------------------------------------------

#ifndef PROTOCOL_TX_BUFFER_SIZE
#define PROTOCOL_TX_BUFFER_SIZE  256
#endif
#define PROTOCOL_RX_MAX_SIZE     512

// ------------------------------------------------------------
// Enumerations (wire vocabulary)
// ------------------------------------------------------------

enum class RobotEmotion : uint8_t {
    NEUTRAL = 0,
    HAPPY,
    CURIOUS,
    SLEEPY,
    THINKING,   // valid internally/dashboard; AI is steered to the 5 core emotions
    SCARED,
};

enum class RobotAnimation : uint8_t {
    NONE = 0,
    SMILE,
    BLINK,
    LOOK_LEFT,
    LOOK_RIGHT,
    TILT,
};

enum class RobotMovement : uint8_t {
    NONE = 0,
    FORWARD_SMALL,
    BACKWARD_SMALL,
    TURN_LEFT,
    TURN_RIGHT,
    STOP,
};

// V2: horizontal zone reported by the camera unit (and mirrored in
// world-state telemetry). Host-testable, so it lives here, not config.h.
enum class TargetZone : uint8_t {
    NONE = 0,
    LEFT,
    CENTER,
    RIGHT,
};

// ------------------------------------------------------------
// Parsed command
// ------------------------------------------------------------

struct RobotCommand {
    RobotEmotion   emotion       = RobotEmotion::NEUTRAL;
    RobotAnimation animation    = RobotAnimation::NONE;
    RobotMovement  movement      = RobotMovement::NONE;
    char           speech[129]   = {0};   // max 128 chars + NUL
    uint8_t        speed         = 0;     // optional PWM override (0 = default)
    uint16_t       durationMs    = 0;     // optional motion duration (0 = default)
    bool           hasAnimation  = false;
    bool           hasMovement   = false;
    bool           hasSpeech     = false;
};

// ------------------------------------------------------------
// Serialization (returns number of bytes written, 0 on failure)
// ------------------------------------------------------------

size_t protocol_serialize_touch_head(char* buffer, size_t capacity);
size_t protocol_serialize_boot_completed(char* buffer, size_t capacity);
size_t protocol_serialize_obstacle_detected(char* buffer, size_t capacity);

// ------------------------------------------------------------
// V2 serializers (ESP32 -> Local AI). Additive; V1 consumers that only
// know the V1 events can safely ignore these.
// ------------------------------------------------------------

size_t protocol_serialize_obstacle_cleared(char* buffer, size_t capacity);
size_t protocol_serialize_person_detected(char* buffer, size_t capacity, TargetZone zone);
size_t protocol_serialize_person_lost(char* buffer, size_t capacity);
size_t protocol_serialize_fall_detected(char* buffer, size_t capacity);

struct WorldStateFields {
    bool        personPresent = false;
    TargetZone  zone          = TargetZone::NONE;
    bool        hasDistance   = false;
    float       distanceCm    = 0.0f;
    bool        obstacle      = false;
    bool        obstacleClose = false;
    bool        upright       = true;
    bool        inverted      = false;
    const char* motion        = "still";
    const char* behaviorState = "UNKNOWN";
    bool        visionOnline  = false;
};

size_t protocol_serialize_world_state(char* buffer, size_t capacity,
                                      const WorldStateFields& f);

struct RobotStatusFields {
    const char* state;      // behavior FSM state name
    const char* emotion;    // current emotion name
    uint32_t    uptimeS;
    int32_t     rssiDbm;
    bool        wifiConnected;
    bool        wsConnected;
};

size_t protocol_serialize_status(char* buffer, size_t capacity,
                                 const RobotStatusFields& fields);

// ------------------------------------------------------------
// Parsing / validation (returns true only for a fully valid command)
// ------------------------------------------------------------

bool protocol_parse_robot_command(const char* payload, size_t length, RobotCommand& out);

// ------------------------------------------------------------
// V2: vision frame (ESP32-CAM unit -> main robot over the vision link)
//   {"type":"vision","person":true,"zone":"center","score":42}
// person is mandatory; zone optional (defaults NONE); score optional
// (raw motion energy, useful for tuning on the dashboard).
// ------------------------------------------------------------

struct VisionFrame {
    bool       person = false;
    TargetZone zone   = TargetZone::NONE;
    uint8_t    score  = 0;      // 0..255 motion energy (clamped)
};

bool protocol_parse_vision_frame(const char* payload, size_t length, VisionFrame& out);

// ------------------------------------------------------------
// Enum <-> string helpers (also used by behavior module)
// ------------------------------------------------------------

const char*      emotion_to_string(RobotEmotion e);
RobotEmotion     emotion_from_string(const char* s);
const char*      animation_to_string(RobotAnimation a);
RobotAnimation   animation_from_string(const char* s);
const char*      movement_to_string(RobotMovement m);
RobotMovement    movement_from_string(const char* s);
const char*      zone_to_string(TargetZone z);
TargetZone       zone_from_string(const char* s);
