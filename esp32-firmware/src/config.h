/**
 * config.h — Centralized configuration for AI Pet Robot V2.
 *
 * ALL hardware pins, timings, thresholds and tunables live here.
 * Never scatter magic numbers across modules.
 */
#pragma once

#include <Arduino.h>

// ============================================================
// BUILD IDENTITY
// ============================================================
#define ROBOT_NAME            "PetRobotV2"
#define FIRMWARE_VERSION      "2.0.0"

// ============================================================
// SERIAL / DEBUG
// ============================================================
#define SERIAL_BAUD           115200

// ============================================================
// TB6612FNG MOTOR DRIVER
// ============================================================
#define PIN_MOTOR_STBY        4     // Standby (active HIGH)
#define PIN_MOTOR_AIN1        16    // Left motor direction 1
#define PIN_MOTOR_AIN2        17    // Left motor direction 2
#define PIN_MOTOR_PWMA        25    // Left motor PWM
#define PIN_MOTOR_BIN1        18    // Right motor direction 1
#define PIN_MOTOR_BIN2        19    // Right motor direction 2
#define PIN_MOTOR_PWMB        26    // Right motor PWM

#define MOTOR_PWM_CHANNEL_A   0     // ESP32 LEDC channel for PWMA
#define MOTOR_PWM_CHANNEL_B   1     // ESP32 LEDC channel for PWMB
#define MOTOR_PWM_FREQ_HZ     5000  // PWM frequency
#define MOTOR_PWM_RESOLUTION  8     // bits (duty 0..255)
#define MOTOR_PWM_MAX_DUTY    255

#define MOTOR_SPEED_MIN       60    // below this the motor usually stalls
#define MOTOR_SPEED_MAX       255
#define MOTOR_SPEED_DEFAULT   180

// ============================================================
// SSD1306 OLED (I2C)
// ============================================================
#define PIN_OLED_SDA          21
#define PIN_OLED_SCL          22
#define OLED_I2C_ADDR         0x3C
#define OLED_WIDTH            128
#define OLED_HEIGHT           64
#define OLED_I2C_FREQ_HZ      400000

// ============================================================
// TTP223 TOUCH SENSOR
// ============================================================
#define PIN_TOUCH_OUT         32
// TTP223 output is HIGH while touched (default config).
#define TOUCH_ACTIVE_LEVEL    HIGH
#define TOUCH_DEBOUNCE_MS     60    // stable time required before accepting edge

// ============================================================
// PASSIVE BUZZER
// ============================================================
#define PIN_BUZZER            27
#define BUZZER_CHANNEL        2     // LEDC channel (independent from motors)
#define BUZZER_PWM_FREQ_HZ    2000
#define BUZZER_PWM_RESOLUTION 10

// ============================================================
// WS2812B LED STRIP
// ============================================================
#define PIN_LED_DATA          33
#define LED_COUNT             8
#define LED_DEFAULT_BRIGHTNESS 80   // 0..255

// ============================================================
// Wi-Fi
// ============================================================
#define WIFI_CONNECT_TIMEOUT_MS 10000  // connection attempt window (~10s)
#define WIFI_MAX_RETRIES_BEFORE_OFFLINE 3

// ============================================================
// WebSocket server (Local AI)
// ============================================================
#define AI_WS_PORT            8765
#define AI_WS_PATH            "/ws/robot"

// Developer-local secrets (copy include/secrets.h.example -> include/secrets.h).
// secrets.h may define: WIFI_SSID, WIFI_PASSWORD, AI_WS_HOST.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD ""
#endif
#ifndef AI_WS_HOST
  #define AI_WS_HOST ""
#endif

#define WS_HEARTBEAT_INTERVAL_MS  3000   // robot announces itself while connected
#define WS_HEARTBEAT_TIMEOUT_MS   5000   // no traffic for this long -> OFFLINE
#define WS_RESPONSE_TIMEOUT_MS    3000   // max wait for AI reply after touch

// Reconnect backoff ladder (ms). Clamped to the last value.
static const uint32_t WS_RECONNECT_BACKOFF_MS[] = {5000, 10000, 20000, 40000, 60000};
#define WS_RECONNECT_BACKOFF_COUNT  (sizeof(WS_RECONNECT_BACKOFF_MS) / sizeof(uint32_t))

// ============================================================
// BEHAVIOR FSM TIMINGS
// ============================================================
#define BOOT_SEQUENCE_MS        2500   // BOOTING -> IDLE
#define THINKING_TIMEOUT_MS     WS_RESPONSE_TIMEOUT_MS
#define HAPPY_DURATION_MS       2000   // HAPPY -> IDLE
#define EXPLORE_DURATION_MS     8000   // EXPLORE -> IDLE (bounded 5-10s)
#define IDLE_TO_SLEEP_MIN_MS    30000  // idle no-input window -> SLEEP
#define IDLE_TO_SLEEP_MAX_MS    60000

// Local personality (offline/online idle life signs)
#define IDLE_BLINK_MIN_MS       2500
#define IDLE_BLINK_MAX_MS       6000
#define IDLE_GLANCE_MIN_MS      4000
#define IDLE_GLANCE_MAX_MS      9000
#define IDLE_MICRO_MOVE_MIN_MS  12000
#define IDLE_MICRO_MOVE_MAX_MS  25000
#define IDLE_CHIRP_MIN_MS       20000
#define IDLE_CHIRP_MAX_MS       45000

// Occasional exploration from IDLE (personality)
#define IDLE_EXPLORE_DECISION_MIN_MS 30000
#define IDLE_EXPLORE_DECISION_MAX_MS 60000
#define EXPLORE_STEP_MS         700     // each small explore motion
#define EXPLORE_STEP_COUNT      6       // 6 steps over ~8s, then IDLE

// Movement limits (safety: every motion is bounded and auto-stops)
#define MOVE_SMALL_MS           350    // "forward_small"/"backward_small" duration
#define MOVE_TURN_MS            300    // "turn_left"/"turn_right" duration
#define MICRO_MOVE_MS           180    // idle personality micro movement
#define MOTOR_WATCHDOG_MS       1500   // absolute cap without renewal

// ============================================================
// PROTOCOL BUFFER SIZES -> live in src/protocol/protocol.h
// (protocol stays host-testable without Arduino.h)
// ============================================================

// ============================================================
// V2: HC-SR04 ULTRASONIC DISTANCE SENSOR
// ECHO is a 5V output -> use a voltage divider (e.g. 1k/2k)
// before feeding GPIO 34 (input-only pin, no pull-ups).
// ============================================================
#define PIN_SONAR_TRIG        23
#define PIN_SONAR_ECHO        34    // input-only

#define SONAR_SAMPLE_MS       66    // ~15 Hz measurement cadence
#define SONAR_ECHO_TIMEOUT_US 25000 // ~4.3 m round trip
#define SONAR_MEDIAN_WINDOW   3     // median-of-3 before smoothing
#define SONAR_EMA_ALPHA_PCT   40    // 0..100, weight of the newest sample
#define SONAR_MAX_VALID_CM    200.0f
#define SONAR_STALE_MS        600   // readings older than this are "no data"

// Obstacle hysteresis (cm). ENTER < CLEAR so a borderline object
// cannot flap the flag.
#define OBSTACLE_ENTER_CM     25.0f
#define OBSTACLE_CLEAR_CM     32.0f
#define OBSTACLE_CLOSE_CM     12.0f  // emergency: stop + surprised

// ============================================================
// V2: IMU (MPU-6050 on the shared OLED I2C bus)
// Address 0x68 (AD0 low) coexists with the SSD1306 at 0x3C.
// Replaceable: see src/imu/imu.h (ImuSensor interface).
// ============================================================
#define IMU_I2C_ADDR          0x68
#define IMU_SAMPLE_MS         20    // 50 Hz
#define IMU_ACCEL_RANGE_G     4.0f
#define IMU_GYRO_RANGE_DPS    500.0f
#define IMU_ANGLE_EMA_PCT     15    // smoothing on accel-derived angles

#define ORIENTATION_UPRIGHT_MAX_DEG  25.0f
#define ORIENTATION_TILT_MIN_DEG     65.0f  // sustained tilt latches a fall
#define ORIENTATION_INVERTED_DEG     100.0f // clearly upside down
#define FALL_CONFIRM_MS              400    // tilt must persist this long
#define IMU_STALE_MS                 500    // no samples -> perception blind

// Motion classification (gyro magnitude, deg/s)
#define MOTION_STILL_MAX_DPS  20.0f
#define IMU_SHOCK_G           2.4f  // accel spike treated as a bump/drop

// ============================================================
// V2: VISION LINK (ESP32-CAM companion unit)
// The camera unit connects HERE as a WebSocket client, so vision
// keeps working whenever Wi-Fi is up — even if the Local AI is down.
// ============================================================
#define VISION_WS_PORT        8766
#define VISION_WS_PATH        "/ws/vision"
#define VISION_LINK_TIMEOUT_MS 3000  // camera silent this long -> offline
#define VISION_PERSON_FRESH_MS 1500  // person flag expires after this

// ============================================================
// V2: PERCEPTION / WORLD STATE
// ============================================================
#define PERSON_CONFIRM_FRAMES 2     // consecutive vision frames to confirm
#define PERSON_LOST_MS        2000  // silence before person counts as lost
#define WORLD_STATE_BROADCAST_MS 5000  // periodic summary to Local AI

// ============================================================
// V2: BEHAVIOR FSM — NEW STATE TIMINGS
// ============================================================
#define CURIOUS_DURATION_MS     2500
#define SURPRISED_DURATION_MS   1200
#define SEARCH_DURATION_MS      5000
#define INTERACT_DURATION_MS    3500
#define AVOID_INSPECT_MS        450    // per look left/right during avoidance
#define AVOID_BACKOFF_MS        350    // small backward step duration
#define FOLLOW_STEP_MS          700    // paced follow hops
#define FOLLOW_MAX_MS           10000  // hard stop to prevent endless chase
#define FOLLOW_COOLDOWN_MS      8000   // no follow again before this passes
#define FOLLOW_MIN_GAP_CM       20.0f  // do not drive closer than this
#define INTERACT_DISTANCE_CM    60.0f  // "person is near" threshold
#define FAULT_RECOVER_MS        1500   // upright this long -> clear fall latch
