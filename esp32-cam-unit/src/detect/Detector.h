/**
 * Detector.h — pluggable presence detection interface.
 *
 * V2 ships a motion-zone baseline (MotionZoneDetector): frame differencing
 * on a downsampled grid with left/center/right zoning. A smarter backend
 * (e.g. ESP-WHO face detection) can replace it by implementing this
 * interface and swapping the instance in main.cpp — nothing else changes.
 */
#pragma once

#include <stdint.h>

#include <esp_camera.h>

enum class Zone : uint8_t {
    NONE = 0,
    LEFT,
    CENTER,
    RIGHT,
};

struct Detection {
    bool    person;     // presence flag (motion-energy baseline in V2)
    Zone    zone;       // strongest active zone
    uint8_t score;      // raw motion energy 0..255
};

class IDetector {
public:
    virtual ~IDetector() = default;
    /// Feed one grayscale frame (fb->buf, w*h). Returns the debounced result.
    virtual Detection process(const camera_fb_t* fb) = 0;
};
