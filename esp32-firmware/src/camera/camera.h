/**
 * camera.h — VisionLink: local WebSocket server for the ESP32-CAM unit.
 *
 * The companion camera unit connects HERE (it is the client), so vision
 * keeps working whenever Wi-Fi is up — even when the Local AI laptop is
 * offline. The link only accepts one camera at a time; a second connection
 * replaces the first.
 */
#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>

#include "../config.h"
#include "../protocol/protocol.h"

class VisionLink {
public:
    /// Called with every valid vision frame (main-loop context).
    using VisionHandler = void (*)(const VisionFrame&);

    VisionLink() : _server(VISION_WS_PORT) {}

    void begin(VisionHandler onVision);
    void update();                       // pump the server + refresh health

    bool cameraOnline() const;           // linked AND frames are fresh
    uint32_t lastFrameMs() const { return _lastFrameMs; }
    const char* cameraIp() const { return _cameraIp; }

private:
    static void wsEventStatic(uint8_t num, WStype_t type,
                              uint8_t* payload, size_t length);
    void onWsEvent(uint8_t num, WStype_t type,
                   uint8_t* payload, size_t length);

    WebSocketsServer _server;
    VisionHandler _onVision   = nullptr;
    bool          _connected  = false;
    uint32_t      _lastFrameMs = 0;
    char          _cameraIp[16] = {0};

    static VisionLink* _instance;
};

extern VisionLink visionLink;
