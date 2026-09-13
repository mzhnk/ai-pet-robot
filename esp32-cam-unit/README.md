# ESP32-CAM Companion Unit (PetRobotCamV2)

Standalone vision unit for AI Pet Robot V2. Runs on an **AI-Thinker
ESP32-CAM** (OV2640, PSRAM required). It detects presence/motion, decides
the strongest horizontal zone (left / center / right) and reports compact
frames to the main robot over WebSocket. It also serves a small MJPEG
stream for eyeballing what it sees.

```
ESP32-CAM (this unit)                ESP32 robot body
┌───────────────────────┐   WS :8766  ┌──────────────────────┐
│ camera -> detection   │ ==========> │ VisionLink server    │ -> perception -> behavior
│ (MotionZoneDetector)  │  frames     │ (src/camera/)        │
│ MJPEG stream :80      │             └──────────────────────┘
└───────────────────────┘
```

## Build & flash

```bash
cp include/secrets.h.example include/secrets.h
# edit: WIFI_SSID, WIFI_PASSWORD, ROBOT_HOST (the robot's LAN IP)
pio run -t upload && pio device monitor
```

The unit, robot and laptop must be on the same Wi-Fi (2.4 GHz). If the
robot is offline, the camera keeps retrying quietly — nothing wedges.

## What it reports (and to whom)

- **To the robot** (WS `…:8766/ws/vision`), every 500 ms or on change:
  `{"type":"vision","person":true,"zone":"center","score":42}`
  Parsing/validation lives in the robot's `protocol_parse_vision_frame` —
  malformed frames are dropped, the robot never crashes on them.
- **To browsers**: MJPEG at `http://<cam-ip>/stream`. The dashboard does
  NOT connect here directly — the Local AI proxies it.

## Honest detection scope

`MotionZoneDetector` is a **motion-energy baseline**: 32x24 downsampled
frame differencing, column-band zoning, debounced presence (3 frames to
confirm, 12 to release). It reacts to any significant movement — it is NOT
identity-aware person detection, and a motionless person is invisible.
A smarter backend (e.g. ESP-WHO face detection) implements `IDetector`
(`src/detect/Detector.h`) and swaps in via `main.cpp` — one file.

Tuning knobs live in `src/config.h` (`DIFF_THRESHOLD` is in
`MotionZoneDetector.cpp`): grid size, cadence, confirm/release frames,
center-band width.

## Structure

```
src/
├── main.cpp                  orchestration + Wi-Fi mini-FSM
├── config.h                  all tunables (port, cadence, thresholds)
├── camera/CameraService.*    esp32-camera init, GRAYSCALE capture, fmt2jpg, MJPEG server
├── detect/Detector.h         IDetector interface + Detection struct
├── detect/MotionZoneDetector.*
└── link/RobotLink.*          WebSocket client + send policy
```

## Notes

- GRAYSCALE capture is deliberate: the detector reads pixels directly and
  `fmt2jpg` converts the same frame for the stream — one capture path.
- `CAMERA_GRAB_LATEST` keeps frames fresh under stream load.
- Board partition: `huge_app` (camera stack + WS client don't fit default).
- Verified: compiles clean on `esp32cam` (RAM 18 %, Flash 32 %). Detection
  quality and stream latency need bench verification per `docs/TESTING.md`
  (M12) — not claimed working without hardware.
