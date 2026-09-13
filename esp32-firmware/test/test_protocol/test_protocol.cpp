/**
 * test_protocol.cpp — native (host) unit tests for the ESP32 wire protocol.
 *
 * Run via test/test_protocol/run_native_tests.sh (uses g++ + ArduinoJson).
 * Covers:
 *   - enum<->string tables
 *   - serialization of every ESP32->AI event
 *   - parsing: valid commands, each enum value, optional fields
 *   - hostile input: broken JSON, wrong types, missing fields,
 *     unknown values (dropped), overlong speech (truncated),
 *     speed/duration clamping, non-object payloads
 */
#include <cstring>
#include <cstdio>
#include <string>

#include <ArduinoJson.h>

#include "protocol.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_failures;                                                       \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__);                 \
        }                                                                       \
    } while (0)

// ------------------------------------------------------------
// Enum tables
// ------------------------------------------------------------

static void test_enum_tables() {
    CHECK(strcmp(emotion_to_string(RobotEmotion::HAPPY), "happy") == 0, "emotion_to_string happy");
    CHECK(strcmp(emotion_to_string(RobotEmotion::NEUTRAL), "neutral") == 0, "emotion_to_string neutral");
    CHECK(emotion_from_string("SLEEPY") == RobotEmotion::SLEEPY, "emotion_from_string case-insensitive");
    CHECK(emotion_from_string("nope") == RobotEmotion::NEUTRAL, "emotion_from_string unknown -> neutral");

    CHECK(strcmp(animation_to_string(RobotAnimation::LOOK_LEFT), "look_left") == 0, "animation_to_string");
    CHECK(animation_from_string("Tilt") == RobotAnimation::TILT, "animation_from_string case-insensitive");
    CHECK(strcmp(movement_to_string(RobotMovement::FORWARD_SMALL), "forward_small") == 0, "movement_to_string");
    CHECK(movement_from_string("STOP") == RobotMovement::STOP, "movement_from_string case-insensitive");
}

// ------------------------------------------------------------
// Serialization
// ------------------------------------------------------------

static void test_serialize_events() {
    char buf[PROTOCOL_TX_BUFFER_SIZE];

    CHECK(protocol_serialize_touch_head(buf, sizeof(buf)) > 0, "touch_head serializes");
    CHECK(std::string(buf).find("\"touch_head\"") != std::string::npos, "touch_head payload");
    CHECK(std::string(buf).find("\"event\"") != std::string::npos, "touch_head has event key");

    CHECK(protocol_serialize_boot_completed(buf, sizeof(buf)) > 0, "boot_completed serializes");
    CHECK(std::string(buf).find("\"boot_completed\"") != std::string::npos, "boot_completed payload");

    CHECK(protocol_serialize_obstacle_detected(buf, sizeof(buf)) > 0, "obstacle serializes");
    CHECK(std::string(buf).find("\"obstacle_detected\"") != std::string::npos, "obstacle payload");

    CHECK(protocol_serialize_touch_head(nullptr, sizeof(buf)) == 0, "null buffer rejected");
    CHECK(protocol_serialize_touch_head(buf, 2) == 0, "tiny buffer rejected");
}

static void test_serialize_status() {
    char buf[PROTOCOL_TX_BUFFER_SIZE];
    RobotStatusFields f{};
    f.state = "IDLE";
    f.emotion = "happy";
    f.uptimeS = 123;
    f.rssiDbm = -58;
    f.wifiConnected = true;
    f.wsConnected = true;

    CHECK(protocol_serialize_status(buf, sizeof(buf), f) > 0, "status serializes");
    std::string json(buf);
    CHECK(json.find("\"heartbeat\"") != std::string::npos, "status is heartbeat event");
    CHECK(json.find("\"uptime_s\":123") != std::string::npos, "uptime present");
    CHECK(json.find("\"rssi\":-58") != std::string::npos, "rssi present");
    CHECK(json.find("\"IDLE\"") != std::string::npos, "state present");
    CHECK(json.find("\"happy\"") != std::string::npos, "emotion present");
}

// ------------------------------------------------------------
// Parsing — happy paths
// ------------------------------------------------------------

static void test_parse_valid_minimal() {
    const char* json = "{\"emotion\":\"happy\"}";
    RobotCommand cmd;
    CHECK(protocol_parse_robot_command(json, std::strlen(json), cmd),
          "minimal command parses");
    CHECK(cmd.emotion == RobotEmotion::HAPPY, "emotion applied");
    CHECK(!cmd.hasAnimation && !cmd.hasMovement && !cmd.hasSpeech, "no optional fields");
}

static void test_parse_full_command() {
    const char* json =
        "{\"emotion\":\"curious\",\"animation\":\"look_left\","
        "\"movement\":\"forward_small\",\"speech\":\"Halo!\","
        "\"speed\":150,\"duration_ms\":400}";
    RobotCommand cmd;
    CHECK(protocol_parse_robot_command(json, std::strlen(json), cmd), "full command parses");
    CHECK(cmd.emotion == RobotEmotion::CURIOUS, "full emotion");
    CHECK(cmd.hasAnimation && cmd.animation == RobotAnimation::LOOK_LEFT, "full animation");
    CHECK(cmd.hasMovement && cmd.movement == RobotMovement::FORWARD_SMALL, "full movement");
    CHECK(cmd.hasSpeech && std::string(cmd.speech) == "Halo!", "full speech");
    CHECK(cmd.speed == 150, "speed parsed");
    CHECK(cmd.durationMs == 400, "duration parsed");
}

static void test_parse_all_enums_roundtrip() {
    const char* emotions[] = {"neutral", "happy", "curious", "sleepy", "thinking", "scared"};
    const RobotEmotion expectedEmotions[] = {
        RobotEmotion::NEUTRAL, RobotEmotion::HAPPY, RobotEmotion::CURIOUS,
        RobotEmotion::SLEEPY, RobotEmotion::THINKING, RobotEmotion::SCARED};

    for (int i = 0; i < 6; ++i) {
        std::string json = std::string("{\"emotion\":\"") + emotions[i] + "\"}";
        RobotCommand cmd;
        CHECK(protocol_parse_robot_command(json.c_str(), json.size(), cmd),
              "emotion accepted");
        CHECK(cmd.emotion == expectedEmotions[i], "emotion maps correctly");
    }

    const char* anims[] = {"smile", "blink", "look_left", "look_right", "tilt"};
    const RobotAnimation expectedAnims[] = {
        RobotAnimation::SMILE, RobotAnimation::BLINK, RobotAnimation::LOOK_LEFT,
        RobotAnimation::LOOK_RIGHT, RobotAnimation::TILT};
    for (int i = 0; i < 5; ++i) {
        std::string json = std::string("{\"emotion\":\"neutral\",\"animation\":\"") +
                           anims[i] + "\"}";
        RobotCommand cmd;
        CHECK(protocol_parse_robot_command(json.c_str(), json.size(), cmd), "anim accepted");
        CHECK(cmd.animation == expectedAnims[i], "anim maps correctly");
    }

    const char* moves[] = {"forward_small", "backward_small", "turn_left",
                           "turn_right", "stop"};
    const RobotMovement expectedMoves[] = {
        RobotMovement::FORWARD_SMALL, RobotMovement::BACKWARD_SMALL,
        RobotMovement::TURN_LEFT, RobotMovement::TURN_RIGHT, RobotMovement::STOP};
    for (int i = 0; i < 5; ++i) {
        std::string json = std::string("{\"emotion\":\"neutral\",\"movement\":\"") +
                           moves[i] + "\"}";
        RobotCommand cmd;
        CHECK(protocol_parse_robot_command(json.c_str(), json.size(), cmd), "move accepted");
        CHECK(cmd.movement == expectedMoves[i], "move maps correctly");
    }
}

// ------------------------------------------------------------
// Parsing — hostile input (must NEVER crash, must reject or degrade)
// ------------------------------------------------------------

static void test_parse_hostile() {
    RobotCommand cmd;

    // Broken / empty / wrong shape
    CHECK(!protocol_parse_robot_command("not json at all", 15, cmd), "prose rejected");
    CHECK(!protocol_parse_robot_command("{\"emotion\":", 11, cmd), "truncated rejected");
    CHECK(!protocol_parse_robot_command("", 0, cmd), "empty rejected");
    CHECK(!protocol_parse_robot_command(nullptr, 5, cmd), "null rejected");
    CHECK(!protocol_parse_robot_command("[1,2,3]", 7, cmd), "array rejected");
    CHECK(!protocol_parse_robot_command("\"just a string\"", 15, cmd), "string rejected");

    // Missing / invalid emotion
    CHECK(!protocol_parse_robot_command("{\"animation\":\"smile\"}", 21, cmd),
          "missing emotion rejected");
    CHECK(!protocol_parse_robot_command("{\"emotion\":\"enraged\"}", 21, cmd),
          "unknown emotion rejected");
    CHECK(!protocol_parse_robot_command("{\"emotion\":42}", 15, cmd),
          "numeric emotion rejected");

    // Unknown optional values: command stays valid, bad field dropped
    CHECK(protocol_parse_robot_command(
              "{\"emotion\":\"happy\",\"animation\":\"dance\"}", 40, cmd),
          "unknown animation tolerated");
    CHECK(cmd.emotion == RobotEmotion::HAPPY && !cmd.hasAnimation,
          "unknown animation dropped");

    CHECK(protocol_parse_robot_command(
              "{\"emotion\":\"happy\",\"movement\":\"fly\"}", 37, cmd),
          "unknown movement tolerated");
    CHECK(cmd.emotion == RobotEmotion::HAPPY && !cmd.hasMovement,
          "unknown movement dropped");

    // Wrong types for optional fields
    CHECK(protocol_parse_robot_command(
              "{\"emotion\":\"happy\",\"speed\":\"fast\",\"duration_ms\":\"slow\"}", 55, cmd),
          "wrong-type optional tolerated");
    CHECK(cmd.speed == 0 && cmd.durationMs == 0, "wrong-type optional ignored");

    // Speech truncation at the 128-char wire limit
    std::string longSpeech = "{\"emotion\":\"neutral\",\"speech\":\"" +
                             std::string(500, 'x') + "\"}";
    CHECK(protocol_parse_robot_command(longSpeech.c_str(), longSpeech.size(), cmd),
          "long speech tolerated");
    CHECK(std::strlen(cmd.speech) == 128, "speech truncated to 128");

    // Speed / duration clamping (defensive: the wire layer clamps,
    // the motor driver re-clamps with its own safety caps)
    std::string over = "{\"emotion\":\"neutral\",\"movement\":\"forward_small\","
                       "\"speed\":9999,\"duration_ms\":99999}";
    CHECK(protocol_parse_robot_command(over.c_str(), over.size(), cmd),
          "overspeed tolerated");
    CHECK(cmd.speed == 255, "speed clamped to 255");
    CHECK(cmd.durationMs == 1500, "duration clamped to 1500");

    std::string under = "{\"emotion\":\"neutral\",\"movement\":\"stop\","
                        "\"speed\":-40,\"duration_ms\":-50}";
    CHECK(protocol_parse_robot_command(under.c_str(), under.size(), cmd),
          "under-range tolerated");
    CHECK(cmd.speed == 0 && cmd.durationMs == 0, "nonpositive optional ignored");

    // Unknown extra fields must not leak through the filter
    CHECK(protocol_parse_robot_command(
              "{\"emotion\":\"happy\",\"admin\":true,\"cmd\":\"rm -rf /\"}", 55, cmd),
          "unknown fields tolerated");
    CHECK(cmd.emotion == RobotEmotion::HAPPY, "unknown fields filtered out");
}

// ------------------------------------------------------------
// V2: zone enum
// ------------------------------------------------------------

static void test_zone_tables() {
    CHECK(strcmp(zone_to_string(TargetZone::LEFT), "left") == 0, "zone_to_string");
    CHECK(strcmp(zone_to_string(TargetZone::CENTER), "center") == 0, "zone_to_string center");
    CHECK(strcmp(zone_to_string(TargetZone::RIGHT), "right") == 0, "zone_to_string right");
    CHECK(zone_from_string("CENTER") == TargetZone::CENTER, "zone case-insensitive");
    CHECK(zone_from_string("bogus") == TargetZone::NONE, "unknown zone -> NONE");
}

// ------------------------------------------------------------
// V2: new event serializers
// ------------------------------------------------------------

static void test_serialize_v2_events() {
    char buf[PROTOCOL_TX_BUFFER_SIZE];

    CHECK(protocol_serialize_obstacle_cleared(buf, sizeof(buf)) > 0, "obstacle_cleared");
    CHECK(std::string(buf).find("\"obstacle_cleared\"") != std::string::npos, "cleared payload");

    CHECK(protocol_serialize_person_detected(buf, sizeof(buf), TargetZone::RIGHT) > 0,
          "person_detected serializes");
    std::string json(buf);
    CHECK(json.find("\"person_detected\"") != std::string::npos, "person payload");
    CHECK(json.find("\"right\"") != std::string::npos, "person zone present");

    CHECK(protocol_serialize_person_detected(buf, sizeof(buf), TargetZone::NONE) > 0,
          "person_detected NONE zone");
    CHECK(std::string(buf).find("\"none\"") != std::string::npos, "NONE zone payload");

    CHECK(protocol_serialize_person_lost(buf, sizeof(buf)) > 0, "person_lost");
    CHECK(std::string(buf).find("\"person_lost\"") != std::string::npos, "person_lost payload");

    CHECK(protocol_serialize_fall_detected(buf, sizeof(buf)) > 0, "fall_detected");
    CHECK(std::string(buf).find("\"fall_detected\"") != std::string::npos, "fall payload");

    CHECK(protocol_serialize_person_detected(nullptr, 16, TargetZone::LEFT) == 0,
          "null buffer rejected (V2)");
}

static void test_serialize_world_state() {
    char buf[PROTOCOL_TX_BUFFER_SIZE];

    WorldStateFields f;
    f.personPresent = true;
    f.zone = TargetZone::CENTER;
    f.hasDistance = true;
    f.distanceCm = 23.45f;
    f.obstacle = false;
    f.obstacleClose = false;
    f.upright = true;
    f.inverted = false;
    f.motion = "moving";
    f.behaviorState = "FOLLOW";
    f.visionOnline = true;

    CHECK(protocol_serialize_world_state(buf, sizeof(buf), f) > 0, "world_state serializes");
    std::string json(buf);
    CHECK(json.find("\"world_state\"") != std::string::npos, "world_state event name");
    CHECK(json.find("\"person\":true") != std::string::npos, "world person");
    CHECK(json.find("\"center\"") != std::string::npos, "world zone");
    CHECK(json.find("\"distance\":23.5") != std::string::npos ||
          json.find("\"distance\":23.4") != std::string::npos, "world distance rounded");
    CHECK(json.find("\"FOLLOW\"") != std::string::npos, "world behavior state");
    CHECK(json.find("\"vision\":true") != std::string::npos, "world vision online");

    // No distance reading -> key omitted
    WorldStateFields g;
    g.hasDistance = false;
    CHECK(protocol_serialize_world_state(buf, sizeof(buf), g) > 0, "world_state w/o distance");
    CHECK(std::string(buf).find("distance") == std::string::npos, "distance omitted");

    // Truncation guard
    CHECK(protocol_serialize_world_state(buf, 8, f) == 0, "tiny buffer rejected");
}

// ------------------------------------------------------------
// V2: vision frame parsing (camera unit -> robot)
// ------------------------------------------------------------

static void test_parse_vision_frames() {
    VisionFrame v;

    const char* full = "{\"type\":\"vision\",\"person\":true,\"zone\":\"left\",\"score\":42}";
    CHECK(protocol_parse_vision_frame(full, std::strlen(full), v), "full vision frame parses");
    CHECK(v.person, "vision person true");
    CHECK(v.zone == TargetZone::LEFT, "vision zone left");
    CHECK(v.score == 42, "vision score");

    const char* minimal = "{\"person\":false}";
    CHECK(protocol_parse_vision_frame(minimal, std::strlen(minimal), v), "minimal frame parses");
    CHECK(!v.person && v.zone == TargetZone::NONE && v.score == 0, "minimal defaults");

    const char* unknownZone = "{\"person\":true,\"zone\":\"upstairs\"}";
    CHECK(protocol_parse_vision_frame(unknownZone, std::strlen(unknownZone), v),
          "unknown zone tolerated");
    CHECK(v.person && v.zone == TargetZone::NONE, "unknown zone -> NONE");

    // Hostile input: never crash, always reject cleanly
    CHECK(!protocol_parse_vision_frame("garbage{{", 9, v), "vision prose rejected");
    CHECK(!protocol_parse_vision_frame("", 0, v), "vision empty rejected");
    CHECK(!protocol_parse_vision_frame(nullptr, 4, v), "vision null rejected");
    CHECK(!protocol_parse_vision_frame("[true]", 6, v), "vision array rejected");
    CHECK(!protocol_parse_vision_frame("{\"zone\":\"left\"}", 15, v),
          "vision missing person rejected");
    CHECK(!protocol_parse_vision_frame("{\"person\":\"yes\"}", 16, v),
          "vision non-bool person rejected");

    // Score clamping
    const char* overScore = "{\"person\":true,\"score\":9999}";
    CHECK(protocol_parse_vision_frame(overScore, std::strlen(overScore), v), "over score tolerated");
    CHECK(v.score == 255, "score clamped high");
    const char* underScore = "{\"person\":true,\"score\":-7}";
    CHECK(protocol_parse_vision_frame(underScore, std::strlen(underScore), v), "under score tolerated");
    CHECK(v.score == 0, "score clamped low");
}

int main() {
    test_enum_tables();
    test_zone_tables();
    test_serialize_events();
    test_serialize_v2_events();
    test_serialize_world_state();
    test_serialize_status();
    test_parse_valid_minimal();
    test_parse_full_command();
    test_parse_all_enums_roundtrip();
    test_parse_hostile();
    test_parse_vision_frames();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
