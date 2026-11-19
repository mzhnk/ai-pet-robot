/**
 * CameraService.cpp — AI-Thinker ESP32-CAM pin map + GRAYSCALE capture +
 * minimal MJPEG server. The MJPEG response format follows the standard
 * multipart/x-mixed-replace layout used by CameraWebServer.
 */
#include "CameraService.h"

#include <img_converters.h>

#include "../config.h"

// AI-Thinker ESP32-CAM fixed pin map.
namespace {
constexpr int PIN_CAMERA_PWDN   = 32;
constexpr int PIN_CAMERA_RESET  = -1;
constexpr int PIN_CAMERA_XCLK   = 0;
constexpr int PIN_CAMERA_SIOD   = 26;
constexpr int PIN_CAMERA_SIOC   = 27;
constexpr int PIN_CAMERA_Y9     = 35;
constexpr int PIN_CAMERA_Y8     = 34;
constexpr int PIN_CAMERA_Y7     = 39;
constexpr int PIN_CAMERA_Y6     = 36;
constexpr int PIN_CAMERA_Y5     = 21;
constexpr int PIN_CAMERA_Y4     = 19;
constexpr int PIN_CAMERA_Y3     = 18;
constexpr int PIN_CAMERA_Y2     = 5;
constexpr int PIN_CAMERA_VSYNC  = 25;
constexpr int PIN_CAMERA_HREF   = 23;
constexpr int PIN_CAMERA_PCLK   = 22;

constexpr int CAMERA_XCLK_MHZ   = 20;

// Macro (not a variable): it is concatenated into the string literal below.
#define MJPEG_BOUNDARY "frame"

const char* STREAM_HEADER =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace;boundary=" MJPEG_BOUNDARY "\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "X-Framerate: 10\r\n"
    "\r\n";
} // namespace

bool CameraService::begin() {
    if (_initialized) return true;

    camera_config_t cfg = {};
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.pin_pwdn     = PIN_CAMERA_PWDN;
    cfg.pin_reset    = PIN_CAMERA_RESET;
    cfg.pin_xclk     = PIN_CAMERA_XCLK;
    cfg.pin_sccb_sda = PIN_CAMERA_SIOD;
    cfg.pin_sccb_scl = PIN_CAMERA_SIOC;
    cfg.pin_d7 = PIN_CAMERA_Y9;
    cfg.pin_d6 = PIN_CAMERA_Y8;
    cfg.pin_d5 = PIN_CAMERA_Y7;
    cfg.pin_d4 = PIN_CAMERA_Y6;
    cfg.pin_d3 = PIN_CAMERA_Y5;
    cfg.pin_d2 = PIN_CAMERA_Y4;
    cfg.pin_d1 = PIN_CAMERA_Y3;
    cfg.pin_d0 = PIN_CAMERA_Y2;
    cfg.pin_vsync = PIN_CAMERA_VSYNC;
    cfg.pin_href  = PIN_CAMERA_HREF;
    cfg.pin_pclk  = PIN_CAMERA_PCLK;

    cfg.xclk_freq_hz = CAMERA_XCLK_MHZ * 1000000;
    cfg.pixel_format = PIXFORMAT_GRAYSCALE;
    cfg.frame_size   = FRAMESIZE_QVGA;      // 320x240
    cfg.jpeg_quality = CAM_JPEG_QUALITY;    // used when converting to JPEG
    cfg.fb_count     = CAM_FB_COUNT;
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;  // always the freshest frame

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        Serial.printf("[cam] init failed: 0x%x\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_vflip(sensor, 0);
        sensor->set_hmirror(sensor, 0);
    }

    _server = WiFiServer(CAM_STREAM_PORT);
    _server.begin();
    _server.setNoDelay(true);

    _initialized = true;
    Serial.printf("[cam] ready, MJPEG stream on :%u%s\n",
                  CAM_STREAM_PORT, CAM_STREAM_PATH);
    return true;
}

void CameraService::end() {
    if (!_initialized) return;
    if (_streamClient) _streamClient.stop();
    _server.stop();
    esp_camera_deinit();
    _initialized = false;
}

camera_fb_t* CameraService::capture() {
    if (!_initialized) return nullptr;

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) return nullptr;

    _frameW = fb->width;
    _frameH = fb->height;

    // Keep a JPEG copy around for the stream client (converted lazily).
    if (_lastJpeg != nullptr) {
        free(_lastJpeg);
        _lastJpeg = nullptr;
        _lastJpegLen = 0;
    }
    if (!fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_GRAYSCALE,
                 CAM_JPEG_QUALITY, &_lastJpeg, &_lastJpegLen)) {
        _lastJpeg = nullptr;
        _lastJpegLen = 0;
    }

    return fb;   // caller must esp_camera_fb_return(fb)
}

void CameraService::serviceStream() {
    if (!_initialized) return;

    // Accept a viewer when idle.
    if (!_streamClient || !_streamClient.connected()) {
        WiFiClient pending = _server.accept();
        if (pending) {
            if (_streamClient) _streamClient.stop();
            _streamClient = pending;
            _streamClient.print(STREAM_HEADER);
            Serial.println("[stream] viewer attached");
        }
    }

    if (!_streamClient || !_streamClient.connected()) return;
    if (_lastJpeg == nullptr || _lastJpegLen == 0) return;

    const char partHeader[] =
        "--" MJPEG_BOUNDARY "\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n";

    char head[96];
    snprintf(head, sizeof(head), partHeader, static_cast<unsigned>(_lastJpegLen));

    const bool ok = _streamClient.write(reinterpret_cast<const uint8_t*>(head),
                                        strlen(head)) &&
                    _streamClient.write(_lastJpeg, _lastJpegLen) &&
                    _streamClient.print("\r\n");
    if (!ok) {
        _streamClient.stop();
        Serial.println("[stream] viewer gone");
    }
}
