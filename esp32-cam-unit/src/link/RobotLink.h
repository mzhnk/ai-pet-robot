/**
 * RobotLink.h — WebSocket client toward the main robot's vision link.
 *
 * Sends compact detection summaries; the library's built-in reconnect
 * keeps trying while Wi-Fi is up. Frame reports go out on change or on a
 * fixed keepalive, whichever comes first.
 */
#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

#include "../detect/Detector.h"

class RobotLink {
public:
    void begin();
    void update();

    bool connected() const { return _connected; }

    /// Queue the latest detection for transmission (main-loop context).
    void report(const Detection& det);

private:
    static void wsEventStatic(WStype_t type, uint8_t* payload, size_t length);
    void onWsEvent(WStype_t type, uint8_t* payload, size_t length);

    WebSocketsClient _ws;
    bool     _connected = false;
    Detection _lastSent = {false, Zone::NONE, 0};
    uint32_t _lastSendMs = 0;
    uint32_t _lastReportMs = 0;

    static RobotLink* _instance;
};
