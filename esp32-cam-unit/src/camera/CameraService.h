/**
 * CameraService.h — esp32-camera init + frame capture + MJPEG HTTP stream.
 *
 * Frames are captured in GRAYSCALE so the detector can read pixels
 * directly; the same frame is converted to JPEG (fmt2jpg) for the
 * optional browser stream. Single capture path, no double buffering
 * tricks — the little ESP32 stays within its budget.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>

class CameraService {
public:
    bool begin();
    void end();

    /// Grab the next frame. Returns nullptr on failure. The frame stays
    /// valid until the next capture() call (frame buffer is recycled).
    camera_fb_t* capture();

    /// Serve one pending HTTP client (call every loop; non-blocking).
    void serviceStream();

private:
    bool      _initialized = false;
    WiFiServer _server;
    WiFiClient _streamClient;      // the one active MJPEG viewer
    uint8_t*   _lastJpeg = nullptr;
    size_t     _lastJpegLen = 0;
    uint16_t   _frameW = 0, _frameH = 0;
};
