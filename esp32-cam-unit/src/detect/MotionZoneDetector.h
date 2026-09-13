/**
 * MotionZoneDetector.h — motion-zone presence baseline (see .cpp).
 */
#pragma once

#include "Detector.h"
#include "../config.h"

class MotionZoneDetector : public IDetector {
public:
    Detection process(const camera_fb_t* fb) override;

private:
    uint8_t  _prevGrid[DETECT_GRID_ROWS][DETECT_GRID_COLS] = {{0}};
    bool     _hasPrev    = false;
    bool     _present    = false;
    uint8_t  _hitStreak  = 0;
    uint8_t  _missStreak = 255;   // start "not present"
};
