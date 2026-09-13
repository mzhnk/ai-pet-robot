/**
 * main.cpp — ESP32-CAM companion unit (PetRobotCamV2).
 *
 * Loop duties, all non-blocking:
 *   1. keep Wi-Fi up (join/retry with its own small state machine)
 *   2. capture a frame on the detection cadence
 *   3. run the detector on the frame, report results to the robot
 *   4. pump the WebSocket + serve the MJPEG stream
 *
 * Failure posture: Wi-Fi down, robot down or camera absent never wedge
 * the loop — the unit just keeps retrying quietly.
 */
#include <Arduino.h>
#include <WiFi.h>

#include "camera/CameraService.h"
#include "config.h"
#include "detect/MotionZoneDetector.h"
#include "link/RobotLink.h"

namespace {

enum class NetState : uint8_t { JOINING, CONNECTED, RETRY_WAIT };

CameraService    camera;
MotionZoneDetector detector;
RobotLink        robotLink;

NetState  _netState = NetState::JOINING;
uint32_t  _netMarkMs = 0;
uint32_t  _nextCaptureMs = 0;

void serviceWifi() {
    const uint32_t now = millis();
    const bool up = WiFi.status() == WL_CONNECTED;

    switch (_netState) {
        case NetState::JOINING:
            if (up) {
                Serial.printf("[net] joined, ip=%s\n",
                              WiFi.localIP().toString().c_str());
                _netState = NetState::CONNECTED;
                robotLink.begin();
            } else if (now - _netMarkMs > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("[net] join timeout, will retry");
                _netState = NetState::RETRY_WAIT;
                _netMarkMs = now;
            }
            break;

        case NetState::CONNECTED:
            if (!up) {
                Serial.println("[net] lost; reconnecting");
                _netState = NetState::JOINING;
                _netMarkMs = now;
            }
            break;

        case NetState::RETRY_WAIT:
            if (now - _netMarkMs >= WIFI_RETRY_MS) {
                Serial.println("[net] retrying join");
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                _netState = NetState::JOINING;
                _netMarkMs = now;
            }
            break;
    }
}

} // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(100);
    Serial.printf("\n[%s v%s] booting...\n", CAM_UNIT_NAME, CAM_FIRMWARE_VERSION);

    if (!camera.begin()) {
        Serial.println("[main] camera absent; stream/detection disabled");
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);   // we own the retry policy
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    _netMarkMs = millis();

    Serial.println("[main] setup complete");
}

void loop() {
    serviceWifi();
    robotLink.update();
    camera.serviceStream();

    const uint32_t now = millis();
    if (now >= _nextCaptureMs) {
        _nextCaptureMs = now + DETECT_INTERVAL_MS;

        camera_fb_t* fb = camera.capture();
        if (fb != nullptr) {
            const Detection det = detector.process(fb);
            esp_camera_fb_return(fb);
            robotLink.report(det);
        }
    }
}
