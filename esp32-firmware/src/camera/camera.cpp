/**
 * camera.cpp — VisionLink implementation.
 *
 * Frames arrive as small JSON summaries (protocol_parse_vision_frame),
 * not as image data: detection runs on the camera unit, only events and
 * zone info cross the link. The MJPEG stream is served by the camera
 * unit itself on its own HTTP port and proxied by the Local AI for the
 * dashboard.
 */
#include "camera.h"

#include "../config.h"

VisionLink visionLink;
VisionLink* VisionLink::_instance = nullptr;

void VisionLink::begin(VisionHandler onVision) {
    _onVision = onVision;
    _instance = this;

    _server.begin();
    _server.onEvent(wsEventStatic);

    Serial.printf("[vision] link server on port %u\n", VISION_WS_PORT);
}

void VisionLink::update() {
    _server.loop();
}

bool VisionLink::cameraOnline() const {
    return _connected && (millis() - _lastFrameMs) < VISION_LINK_TIMEOUT_MS;
}

void VisionLink::wsEventStatic(uint8_t num, WStype_t type,
                               uint8_t* payload, size_t length) {
    if (_instance != nullptr) {
        _instance->onWsEvent(num, type, payload, length);
    }
}

void VisionLink::onWsEvent(uint8_t num, WStype_t type,
                           uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            const String ip = _server.remoteIP(num).toString();
            strncpy(_cameraIp, ip.c_str(), sizeof(_cameraIp) - 1);
            _cameraIp[sizeof(_cameraIp) - 1] = '\0';
            _connected  = true;
            _lastFrameMs = millis();
            Serial.printf("[vision] camera unit connected from %s\n", _cameraIp);
            break;
        }

        case WStype_DISCONNECTED:
            _connected = false;
            Serial.println("[vision] camera unit disconnected");
            break;

        case WStype_TEXT: {
            VisionFrame frame;
            if (!protocol_parse_vision_frame(reinterpret_cast<const char*>(payload),
                                             length, frame)) {
                Serial.printf("[vision] ignored invalid vision payload (%u bytes)\n",
                              static_cast<unsigned>(length));
                break;
            }
            _lastFrameMs = millis();
            if (_onVision != nullptr) _onVision(frame);
            break;
        }

        default:
            break;   // ping/pong/fragment handled by the library
    }
}
