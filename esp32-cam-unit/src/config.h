/**
 * config.h — ESP32-CAM companion unit configuration.
 *
 * The unit joins the same Wi-Fi as the main robot and reports detection
 * frames over WebSocket. Credentials live in include/secrets.h (copy from
 * include/secrets.h.example).
 */
#pragma once

#define CAM_UNIT_NAME        "PetRobotCamV2"
#define CAM_FIRMWARE_VERSION "2.0.0"

#define SERIAL_BAUD          115200

// ------------------------------------------------------------
// Wi-Fi + robot address
// Developer-local secrets (copy include/secrets.h.example -> include/secrets.h).
// secrets.h may define: WIFI_SSID, WIFI_PASSWORD, ROBOT_HOST.
// ------------------------------------------------------------
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD ""
#endif
#ifndef ROBOT_HOST
  #define ROBOT_HOST ""        // e.g. "192.168.1.20"
#endif

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RETRY_MS           5000

// Main robot's vision link (VISION_WS_PORT in esp32-firmware/src/config.h).
#define ROBOT_VISION_PORT      8766
#define ROBOT_RECONNECT_MS     5000   // lib-level reconnect safety net

// ------------------------------------------------------------
// Camera capture
// ------------------------------------------------------------
#define CAM_PIXEL_FORMAT       1      // 1 = GRAYSCALE (direct pixel access for detection)
#define CAM_FRAME_QVGA         1      // 320x240: fast detection + enough detail
#define CAM_JPEG_QUALITY       12     // 0-63, lower = better
#define CAM_FB_COUNT           2
#define CAM_STREAM_PORT        80     // MJPEG HTTP stream served by this unit
#define CAM_STREAM_PATH        "/stream"
#define CAM_STREAM_MAX_CLIENTS 1      // single viewer keeps the little SoC happy

// ------------------------------------------------------------
// Detection (motion-zone presence baseline)
// ------------------------------------------------------------
#define DETECT_GRID_COLS       32     // downsampled comparison grid
#define DETECT_GRID_ROWS       24
#define DETECT_INTERVAL_MS     150    // detection cadence (~6.7 fps)
#define DETECT_PRESENT_FRAMES  3      // consecutive hits to confirm presence
#define DETECT_LOST_FRAMES     12     // consecutive misses to drop presence
#define DETECT_ZONE_MARGIN_PCT 30     // center band width for CENTER zone
