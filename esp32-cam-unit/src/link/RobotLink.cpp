/**
 * RobotLink.cpp — detection summary sender.
 *
 * Wire format (mirrors protocol_parse_vision_frame on the robot):
 *   {"type":"vision","person":true,"zone":"center","score":42}
 * Send policy: immediately on any meaningful change, else a keepalive
 * every VISION_REPORT_MS so the robot can tell "camera alive" from
 * "camera silent" even in a still room.
 */
#include "RobotLink.h"

#include <WiFi.h>

#include "../config.h"

RobotLink* RobotLink::_instance = nullptr;

namespace {
constexpr uint32_t VISION_REPORT_MS    = 500;   // keepalive cadence
constexpr uint8_t  ZONE_CHANGE_MIN_HIT = 3;     // ignore 1-frame zone flicker
} // namespace

void RobotLink::begin() {
    _instance = this;

    _ws.begin(ROBOT_HOST, ROBOT_VISION_PORT, "/ws/vision");
    _ws.onEvent(wsEventStatic);
    _ws.setReconnectInterval(ROBOT_RECONNECT_MS);
    _ws.enableHeartbeat(15000, 3000, 2);   // ping/pong liveness

    Serial.printf("[link] targeting robot at %s:%u\n",
                  ROBOT_HOST, ROBOT_VISION_PORT);
}

void RobotLink::update() {
    _ws.loop();

    // Keepalive when nothing changed for a while.
    if (_connected && _lastReportMs != 0 &&
        millis() - _lastSendMs >= VISION_REPORT_MS) {
        report(_lastSent);
    }
}

void RobotLink::report(const Detection& det) {
    if (!_connected) return;

    // Suppress noise: identical person+zone does not need resending until
    // the keepalive window; a real zone change needs a few confirming
    // reports in the same zone (handled by the detector's debouncing).
    const bool changed = det.person != _lastSent.person || det.zone != _lastSent.zone;
    const uint32_t now = millis();
    if (!changed && now - _lastSendMs < VISION_REPORT_MS) {
        _lastReportMs = now;
        return;
    }

    static const char* zoneNames[] = {"none", "left", "center", "right"};
    char buf[96];
    const int len = snprintf(buf, sizeof(buf),
                             "{\"type\":\"vision\",\"person\":%s,\"zone\":\"%s\","
                             "\"score\":%u}",
                             det.person ? "true" : "false",
                             zoneNames[static_cast<uint8_t>(det.zone)],
                             static_cast<unsigned>(det.score));
    if (len > 0) {
        _ws.sendTXT(buf, len);
        _lastSent = det;
        _lastSendMs = now;
        _lastReportMs = now;
    }
}

void RobotLink::wsEventStatic(WStype_t type, uint8_t* payload, size_t length) {
    if (_instance != nullptr) {
        _instance->onWsEvent(type, payload, length);
    }
}

void RobotLink::onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    (void)payload;
    (void)length;
    switch (type) {
        case WStype_CONNECTED:
            _connected = true;
            Serial.println("[link] connected to robot vision link");
            break;
        case WStype_DISCONNECTED:
            _connected = false;
            Serial.println("[link] disconnected from robot");
            break;
        default:
            break;
    }
}
