/**
 * MotionZoneDetector.cpp — motion-energy presence baseline.
 *
 * Algorithm (deliberately simple, deterministic and debuggable):
 *  1. Downsample the grayscale frame onto a COLS x ROWS mean grid.
 *  2. Per-cell |current - previous| -> active if above the threshold.
 *  3. Column bands give the zone; the strongest band wins.
 *  4. Presence debouncing: PRESENT_FRAMES consecutive hits to confirm,
 *     LOST_FRAMES consecutive misses to release (no single-frame flapping).
 *
 * Honest scope: this is motion-based presence, NOT identity-aware person
 * detection. It reacts to any significant movement; a static silent person
 * is invisible to it. Upgrade path: swap in an ESP-WHO detector via
 * IDetector (see Detector.h).
 */
#include "MotionZoneDetector.h"

#include <string.h>

#include "../config.h"

namespace {
constexpr uint8_t DIFF_THRESHOLD = 12;   // per-cell gray delta worth counting

uint8_t cellMean(const uint8_t* buf, uint16_t frameW, uint16_t frameH,
                 uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint32_t sum = 0;
    uint32_t count = 0;
    // Stride 2x2 inside the cell: enough precision at 1/4 the work.
    for (uint16_t y = y0; y < y1; y += 2) {
        const uint8_t* row = buf + y * frameW;
        for (uint16_t x = x0; x < x1; x += 2) {
            sum += row[x];
            ++count;
        }
    }
    return (count > 0) ? static_cast<uint8_t>(sum / count) : 0;
}
} // namespace

Detection MotionZoneDetector::process(const camera_fb_t* fb) {
    Detection out = {false, Zone::NONE, 0};

    if (fb == nullptr || fb->width == 0 || fb->height == 0) return out;

    const uint16_t cellW = fb->width / DETECT_GRID_COLS;
    const uint16_t cellH = fb->height / DETECT_GRID_ROWS;
    if (cellW == 0 || cellH == 0) return out;

    uint16_t colHits[DETECT_GRID_COLS] = {0};
    uint16_t totalHits = 0;

    for (uint8_t cy = 0; cy < DETECT_GRID_ROWS; ++cy) {
        const uint16_t y0 = cy * cellH;
        for (uint8_t cx = 0; cx < DETECT_GRID_COLS; ++cx) {
            const uint16_t x0 = cx * cellW;
            const uint8_t mean = cellMean(fb->buf, fb->width, fb->height,
                                          x0, y0, x0 + cellW, y0 + cellH);
            const uint8_t prev = _prevGrid[cy][cx];
            _prevGrid[cy][cx] = mean;

            if (_hasPrev && static_cast<uint8_t>(abs(static_cast<int>(mean) -
                                                     static_cast<int>(prev))) >
                                 DIFF_THRESHOLD) {
                ++colHits[cx];
                ++totalHits;
            }
        }
    }
    _hasPrev = true;

    // ---- debounced presence ----
    if (totalHits > 0) {
        if (_hitStreak < 255) ++_hitStreak;
        _missStreak = 0;
    } else {
        if (_missStreak < 255) ++_missStreak;
        _hitStreak = 0;
    }

    if (!_present && _hitStreak >= DETECT_PRESENT_FRAMES) {
        _present = true;
    } else if (_present && _missStreak >= DETECT_LOST_FRAMES) {
        _present = false;
    }
    out.person = _present;

    // ---- zone (strongest band) + score ----
    out.score = (totalHits > 255) ? 255 : static_cast<uint8_t>(totalHits);
    if (totalHits == 0) return out;

    const uint16_t margin = (DETECT_GRID_COLS * DETECT_ZONE_MARGIN_PCT) / 100;
    uint16_t left = 0, center = 0, right = 0;
    for (uint8_t cx = 0; cx < DETECT_GRID_COLS; ++cx) {
        if (cx < margin)                    left += colHits[cx];
        else if (cx >= DETECT_GRID_COLS - margin) right += colHits[cx];
        else                                center += colHits[cx];
    }

    if (left >= center && left >= right)      out.zone = Zone::LEFT;
    else if (center >= right)                 out.zone = Zone::CENTER;
    else                                      out.zone = Zone::RIGHT;

    return out;
}
