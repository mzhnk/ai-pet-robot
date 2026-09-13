/**
 * protocol.cpp — ArduinoJson-based implementation of the wire protocol.
 *
 * Design rules:
 *  - Every serializer writes into a caller-provided bounded buffer.
 *  - The parser uses a whitelist filter: unknown fields never reach us,
 *    invalid values reject the whole command.
 *  - Case-insensitive enum matching is done with a tiny portable helper
 *    (no strcasecmp dependency so the code builds natively on host too).
 */
#include "protocol.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

// Case-insensitive ASCII compare (portable, no locale, no strcasecmp).
bool equalsIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a && *b) {
        char ca = static_cast<char>(*a | 0x20);
        char cb = static_cast<char>(*b | 0x20);
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == *b;
}

struct EmotionEntry { const char* name; RobotEmotion value; };
struct AnimEntry    { const char* name; RobotAnimation value; };
struct MoveEntry    { const char* name; RobotMovement value; };
struct ZoneEntry    { const char* name; TargetZone value; };

const EmotionEntry EMOTION_TABLE[] = {
    {"neutral",  RobotEmotion::NEUTRAL},
    {"happy",    RobotEmotion::HAPPY},
    {"curious",  RobotEmotion::CURIOUS},
    {"sleepy",   RobotEmotion::SLEEPY},
    {"thinking", RobotEmotion::THINKING},
    {"scared",   RobotEmotion::SCARED},
};

const AnimEntry ANIMATION_TABLE[] = {
    {"none",      RobotAnimation::NONE},
    {"smile",     RobotAnimation::SMILE},
    {"blink",     RobotAnimation::BLINK},
    {"look_left", RobotAnimation::LOOK_LEFT},
    {"look_right",RobotAnimation::LOOK_RIGHT},
    {"tilt",      RobotAnimation::TILT},
};

const MoveEntry MOVEMENT_TABLE[] = {
    {"none",          RobotMovement::NONE},
    {"forward_small", RobotMovement::FORWARD_SMALL},
    {"backward_small",RobotMovement::BACKWARD_SMALL},
    {"turn_left",     RobotMovement::TURN_LEFT},
    {"turn_right",    RobotMovement::TURN_RIGHT},
    {"stop",          RobotMovement::STOP},
};

const ZoneEntry ZONE_TABLE[] = {
    {"none",   TargetZone::NONE},
    {"left",   TargetZone::LEFT},
    {"center", TargetZone::CENTER},
    {"right",  TargetZone::RIGHT},
};

template <typename T, typename Table>
bool lookup(const Table& table, size_t count, const char* name, T& out) {
    for (size_t i = 0; i < count; ++i) {
        if (equalsIgnoreCase(table[i].name, name)) {
            out = table[i].value;
            return true;
        }
    }
    return false;
}

template <typename T, typename Table>
const char* reverseLookup(const Table& table, size_t count, T value) {
    for (size_t i = 0; i < count; ++i) {
        if (table[i].value == value) return table[i].name;
    }
    return "unknown";
}

} // namespace

// ------------------------------------------------------------
// Enum <-> string
// ------------------------------------------------------------

const char* emotion_to_string(RobotEmotion e) {
    return reverseLookup(EMOTION_TABLE, sizeof(EMOTION_TABLE) / sizeof(EmotionEntry), e);
}

RobotEmotion emotion_from_string(const char* s) {
    RobotEmotion out = RobotEmotion::NEUTRAL;
    lookup(EMOTION_TABLE, sizeof(EMOTION_TABLE) / sizeof(EmotionEntry), s, out);
    return out;
}

const char* animation_to_string(RobotAnimation a) {
    return reverseLookup(ANIMATION_TABLE, sizeof(ANIMATION_TABLE) / sizeof(AnimEntry), a);
}

RobotAnimation animation_from_string(const char* s) {
    RobotAnimation out = RobotAnimation::NONE;
    lookup(ANIMATION_TABLE, sizeof(ANIMATION_TABLE) / sizeof(AnimEntry), s, out);
    return out;
}

const char* movement_to_string(RobotMovement m) {
    return reverseLookup(MOVEMENT_TABLE, sizeof(MOVEMENT_TABLE) / sizeof(MoveEntry), m);
}

RobotMovement movement_from_string(const char* s) {
    RobotMovement out = RobotMovement::NONE;
    lookup(MOVEMENT_TABLE, sizeof(MOVEMENT_TABLE) / sizeof(MoveEntry), s, out);
    return out;
}

const char* zone_to_string(TargetZone z) {
    return reverseLookup(ZONE_TABLE, sizeof(ZONE_TABLE) / sizeof(ZoneEntry), z);
}

TargetZone zone_from_string(const char* s) {
    TargetZone out = TargetZone::NONE;
    lookup(ZONE_TABLE, sizeof(ZONE_TABLE) / sizeof(ZoneEntry), s, out);
    return out;
}

// ------------------------------------------------------------
// Serialization
// ------------------------------------------------------------

namespace {

size_t serializeJsonEvent(char* buffer, size_t capacity, JsonDocument& doc) {
    if (buffer == nullptr || capacity == 0) return 0;
    size_t written = serializeJson(doc, buffer, capacity);
    if (written >= capacity) return 0;   // truncated -> treat as failure
    return written;
}

} // namespace

size_t protocol_serialize_touch_head(char* buffer, size_t capacity) {
    JsonDocument doc;
    doc["event"] = "touch_head";
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_boot_completed(char* buffer, size_t capacity) {
    JsonDocument doc;
    doc["event"] = "boot_completed";
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_obstacle_detected(char* buffer, size_t capacity) {
    JsonDocument doc;
    doc["event"] = "obstacle_detected";
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_obstacle_cleared(char* buffer, size_t capacity) {
    JsonDocument doc;
    doc["event"] = "obstacle_cleared";
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_person_detected(char* buffer, size_t capacity,
                                          TargetZone zone) {
    JsonDocument doc;
    doc["event"] = "person_detected";
    doc["zone"]  = zone_to_string(zone);
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_person_lost(char* buffer, size_t capacity) {
    JsonDocument doc;
    doc["event"] = "person_lost";
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_fall_detected(char* buffer, size_t capacity) {
    JsonDocument doc;
    doc["event"] = "fall_detected";
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_world_state(char* buffer, size_t capacity,
                                      const WorldStateFields& f) {
    JsonDocument doc;
    doc["event"]    = "world_state";
    doc["person"]   = f.personPresent;
    doc["zone"]     = zone_to_string(f.zone);
    doc["obstacle"] = f.obstacle;
    doc["close"]    = f.obstacleClose;
    doc["upright"]  = f.upright;
    doc["motion"]   = (f.motion   != nullptr) ? f.motion   : "unknown";
    doc["state"]    = (f.behaviorState != nullptr) ? f.behaviorState : "UNKNOWN";
    doc["vision"]   = f.visionOnline;
    if (f.hasDistance) {
        // One decimal place, rounded explicitly (avoids float noise on the wire).
        doc["distance"] = static_cast<long>(f.distanceCm * 10.0f + 0.5f) / 10.0f;
    }
    return serializeJsonEvent(buffer, capacity, doc);
}

size_t protocol_serialize_status(char* buffer, size_t capacity,
                                 const RobotStatusFields& f) {
    JsonDocument doc;
    doc["event"]       = "heartbeat";
    doc["uptime_s"]    = f.uptimeS;
    doc["rssi"]        = f.rssiDbm;
    doc["wifi"]        = f.wifiConnected;
    doc["ws"]          = f.wsConnected;
    doc["state"]       = (f.state   != nullptr) ? f.state   : "UNKNOWN";
    doc["emotion"]     = (f.emotion != nullptr) ? f.emotion : "neutral";
    return serializeJsonEvent(buffer, capacity, doc);
}

// ------------------------------------------------------------
// Parsing / validation
// ------------------------------------------------------------

bool protocol_parse_robot_command(const char* payload, size_t length, RobotCommand& out) {
    if (payload == nullptr || length == 0) return false;

    // Whitelist filter: ignore any unknown top-level fields.
    JsonDocument filter;
    filter["emotion"]     = true;
    filter["animation"]   = true;
    filter["movement"]    = true;
    filter["speech"]      = true;
    filter["speed"]       = true;
    filter["duration_ms"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length,
                                               DeserializationOption::Filter(filter));
    if (err) return false;                 // malformed JSON -> rejected
    if (!doc["emotion"].is<const char*>()) return false;   // emotion is mandatory

    const char* emotionStr = doc["emotion"];
    RobotEmotion emotion;
    if (!lookup(EMOTION_TABLE, sizeof(EMOTION_TABLE) / sizeof(EmotionEntry),
                emotionStr, emotion)) {
        return false;                      // unknown emotion -> rejected
    }

    out = RobotCommand{};                  // clean slate
    out.emotion = emotion;

    if (doc["animation"].is<const char*>()) {
        RobotAnimation anim;
        if (lookup(ANIMATION_TABLE, sizeof(ANIMATION_TABLE) / sizeof(AnimEntry),
                   doc["animation"], anim)) {
            out.animation    = anim;
            out.hasAnimation = (anim != RobotAnimation::NONE);
        }
        // Unknown animation values are dropped, the command stays valid.
    }

    if (doc["movement"].is<const char*>()) {
        RobotMovement move;
        if (lookup(MOVEMENT_TABLE, sizeof(MOVEMENT_TABLE) / sizeof(MoveEntry),
                   doc["movement"], move)) {
            out.movement    = move;
            out.hasMovement = (move != RobotMovement::NONE);
        }
    }

    if (doc["speech"].is<const char*>()) {
        const char* speech = doc["speech"];
        if (speech != nullptr && speech[0] != '\0') {
            strncpy(out.speech, speech, sizeof(out.speech) - 1);
            out.speech[sizeof(out.speech) - 1] = '\0';
            out.hasSpeech = true;
        }
    }

    // Optional motion tuning from the dashboard (clamped here and
    // re-clamped by the motor driver's safety caps).
    if (doc["speed"].is<int>()) {
        const long v = doc["speed"].as<long>();
        if (v > 0) {
            out.speed = (v > 255) ? 255 : static_cast<uint8_t>(v);
        }
    }
    if (doc["duration_ms"].is<int>()) {
        const long v = doc["duration_ms"].as<long>();
        if (v > 0) {
            out.durationMs = (v > 1500) ? 1500 : static_cast<uint16_t>(v);
        }
    }

    return true;
}

// ------------------------------------------------------------
// V2: vision frame (camera unit -> robot)
// ------------------------------------------------------------

bool protocol_parse_vision_frame(const char* payload, size_t length,
                                 VisionFrame& out) {
    if (payload == nullptr || length == 0) return false;

    JsonDocument filter;
    filter["type"]   = true;
    filter["person"] = true;
    filter["zone"]   = true;
    filter["score"]  = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length,
                                               DeserializationOption::Filter(filter));
    if (err) return false;                          // malformed JSON -> rejected
    if (!doc["person"].is<bool>()) return false;    // person is mandatory

    out = VisionFrame{};
    out.person = doc["person"].as<bool>();

    if (doc["zone"].is<const char*>()) {
        // Unknown zone strings degrade to NONE; the frame stays valid.
        out.zone = zone_from_string(doc["zone"]);
    }

    if (doc["score"].is<int>()) {
        const long v = doc["score"].as<long>();
        out.score = (v < 0) ? 0 : (v > 255) ? 255 : static_cast<uint8_t>(v);
    }

    return true;
}
