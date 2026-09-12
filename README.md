<div align="center">

# 🤖 AI Pet Robot V2 — *"Make It Aware"*

**A DIY desktop pet robot with a local personality, a perception stack, and an LLM brain.**
V1 made it *alive*. V2 makes it **aware**: it sees people, feels obstacles, and knows when it has fallen over — while remaining 100 % functional without any network.

![version](https://img.shields.io/badge/version-2.0.0-blue)
![platform](https://img.shields.io/badge/platform-ESP32_·_ESP32--CAM-orange)
![framework](https://img.shields.io/badge/firmware-PlatformIO_+_Arduino-teal)
![backend](https://img.shields.io/badge/brain-Flask_+_WebSocket_+_OpenRouter-green)
![tests](https://img.shields.io/badge/tests-134_native_·_51_pytest_·_21_E2E-brightgreen)

</div>

---

## Highlights

| | |
|---|---|
| 🧠 **Personality that survives offline** | Wi-Fi dies, laptop sleeps — the pet keeps blinking, chirping, exploring and reconnecting. No reboots, no freezes. |
| 👀 **Vision** | An AI-Thinker ESP32-CAM companion unit detects presence, reports the strongest zone (left / center / right) and streams a live MJPEG preview. |
| 📏 **Distance & obstacle sense** | HC-SR04 sonar with ISR timing, median + EMA filtering and hysteresis — the robot stops, inspects and backs away from obstacles. |
| 🤸 **Balance sense** | MPU-6050 IMU (no external library, raw-register driver) detects upright / tilted / inverted, bumps and falls. |
| 🎭 **13-state Behavior FSM** | CURIOUS, FOLLOW, INTERACT, SEARCH, SURPRISED, AVOID_OBSTACLE, FAULT — on top of all six V1 states, which are preserved exactly. |
| 💬 **LLM brain with guardrails** | OpenRouter fallback chain (Gemini → Groq → Mistral → Qwen) that can *suggest* behavior but can never bypass safety. |
| 🖥️ **Developer dashboard** | Live STATUS / VISION / DISTANCE / IMU / WORLD STATE / BEHAVIOR panels, manual command console and perception-event simulation. |
| 🛡️ **Deterministic safety** | A fall or a close obstacle stops the motors *before* any FSM or AI logic runs. Every motion is duration-capped with a 1.5 s watchdog. |

---

## Table of Contents

1. [Architecture](#architecture)
2. [Repository Layout — every file explained](#repository-layout--every-file-explained)
3. [Hardware](#hardware)
4. [Getting Started](#getting-started)
5. [Configuration Reference](#configuration-reference)
6. [Using the Robot & Dashboard](#using-the-robot--dashboard)
7. [Communication Protocol](#communication-protocol)
8. [Behavior FSM (13 states)](#behavior-fsm-13-states)
9. [Perception Pipeline](#perception-pipeline)
10. [Testing & Verification](#testing--verification)
11. [Troubleshooting](#troubleshooting)
12. [Extending the Project](#extending-the-project)
13. [Design Principles](#design-principles)
14. [Roadmap](#roadmap)
15. [Documentation Map](#documentation-map)
16. [Built With](#built-with)
17. [License](#license)

---

## Architecture

Three cooperating pieces on the same Wi-Fi (2.4 GHz):

```
┌────────────────────────────────┐        Wi-Fi / WebSocket :8765      ┌──────────────────────────┐
│  ESP32 robot body              │ <=================================> │  Laptop — Local AI       │
│                                │   events: touch, person, obstacle,  │  routes/robot_ws.py      │
│  Sensors                       │   fall, world_state, heartbeat      │  robot_behavior.py       │
│    │                           │   commands: emotion / animation /   │  ai_router (OpenRouter)  │
│    v                           │              movement / speech      │  camera stream proxy     │
│  Perception ─> WorldState      │                                     │  Flask + Dashboard       │
│    │                           │                                     └──────────────────────────┘
│    v                           │
│  Behavior FSM (13 states)      │        Wi-Fi / WebSocket :8766      ┌──────────────────────────┐
│  Connection FSM (separate)     │ <================================== │  ESP32-CAM companion     │
│  motors / OLED / LED /         │   frames: person, zone, score       │  detection + MJPEG :80   │
│  touch / sound                 │                                     └──────────────────────────┘
└────────────────────────────────┘
```

**Two rules shape everything:**

1. **The golden rule (from V1):** the robot must keep living when Wi-Fi, WebSocket or the laptop disappears — local personality, touch reactions and safe micro-movements never stop, and reconnects happen without a reboot.
2. **The V2 safety rule:** perception may *suggest* behavior, but safety-critical reactions are deterministic and local. In `main.cpp`, a latched fall or a close obstacle forces `motors.stop()` **before** any FSM or AI command is evaluated. The AI can never drive the robot into an obstacle or move a fallen robot.

```
HC-SR04 ──┐
MPU-6050 ─┼─> PerceptionEngine ──> WorldState ──> Behavior FSM ──> motors / OLED / LED / sound
ESP32-CAM ┘   (filter, debounce,     (shared        (13 states)     │
               hysteresis, events)    belief)                       └─> events + world_state ──> Local AI
```

No sensor drives behavior directly — everything is fused into one shared `WorldState` and expressed as debounced, hysteresis-protected events.

---

## Repository Layout — every file explained

This is the complete content of `ai-pet-robot-v2.zip` (104 entries):

```
├── README.md                             ← this file
├── docs/
│   └── TESTING.md                        Manual hardware procedures M0–M15 + final acceptance checklist (V1 core + V2 perception)
│
├── esp32-firmware/                       ── ROBOT BODY (PlatformIO, board: esp32dev) ──
│   ├── platformio.ini                    Build config: esp32dev env + native test env; ArduinoJson, WebSockets, Adafruit libs
│   ├── README.md                         Firmware deep-dive: module map, serial log tags, build stats
│   ├── include/
│   │   └── secrets.h.example             Template → copy to secrets.h (Wi-Fi SSID/password + laptop IP)
│   ├── src/
│   │   ├── main.cpp                      Thin orchestrator only: init, perception wiring, deterministic safety guards, FSM pumps
│   │   ├── config.h                      ALL pins, timings and thresholds in one place (V1 + V2 sections)
│   │   ├── behavior/behavior.{h,cpp}     13-state Behavior FSM: personality, touch/AI routing, perception reactions
│   │   ├── camera/camera.{h,cpp}         V2 VisionLink: WebSocket server on :8766 that the camera unit connects to
│   │   ├── connection/connection.{h,cpp} Connection FSM: Wi-Fi, WS transport, heartbeat, backoff ladder (untouched by V2)
│   │   ├── display/display.{h,cpp}       SSD1306 face system: 7 faces + blink/look/smile/tilt one-shots + ASCII speech bar
│   │   ├── distance/distance.{h,cpp}     V2 HC-SR04: non-blocking ISR echo, median-of-3 + EMA, hysteresis obstacle flags
│   │   ├── imu/imu.{h,cpp}               V2 ImuSensor interface + MPU-6050 raw-register driver (swap chip = edit imu.cpp only)
│   │   ├── led/led.{h,cpp}               WS2812B mood light: off/solid/pulse/thinking/happy/error modes
│   │   ├── motor/motor.{h,cpp}           TB6612 driver: LEDC PWM, duration caps, 1.5 s watchdog, STOP default
│   │   ├── perception/perception.{h,cpp} V2 fusion engine: WorldState updates + debounced event queue (FIFO, 8 deep)
│   │   ├── protocol/protocol.{h,cpp}     JSON wire protocol: strict whitelist validation, host-testable (no Arduino.h)
│   │   ├── sound/sound.{h,cpp}           Non-blocking buzzer sequences: startup/happy/thinking/error/beep
│   │   ├── touch/touch.{h,cpp}           TTP223 sampling with 60 ms debounce → exactly one event per touch
│   │   └── world/world_state.{h,cpp}     V2 shared WorldState struct: the robot's single belief about the world
│   └── test/test_protocol/
│       ├── test_protocol.cpp             134 native checks: commands, clamps, V2 serializers, hostile JSON
│       └── run_native_tests.sh           Builds & runs the suite with g++ — no hardware required
│
├── esp32-cam-unit/                       ── COMPANION VISION UNIT (PlatformIO, board: esp32cam / AI-Thinker) ──
│   ├── platformio.ini                    esp32cam env: PSRAM flags + huge_app partition (camera stack needs it)
│   ├── README.md                         Camera-unit deep-dive: detection scope, tuning knobs, stream notes
│   ├── include/
│   │   └── secrets.h.example             Template → copy to secrets.h (Wi-Fi + ROBOT_HOST = robot's LAN IP)
│   └── src/
│       ├── main.cpp                      Orchestration + Wi-Fi mini-FSM (quiet retry when the robot is offline)
│       ├── config.h                      Camera/detection/link tunables (ports, cadence, confirm/release frames)
│       ├── camera/CameraService.{h,cpp}  esp32-camera init, GRAYSCALE QVGA capture, fmt2jpg, MJPEG server on :80/stream
│       ├── detect/Detector.h             IDetector interface + Detection struct — swap detection backends here
│       ├── detect/MotionZoneDetector.{h,cpp}  32×24 frame-differencing motion detector with left/center/right zone bands
│       └── link/RobotLink.{h,cpp}        WebSocket client → robot vision link, change + keepalive send policy
│
└── local-ai/                             ── LAPTOP-SIDE BRAIN (Flask + WebSocket + dashboard) ──
    ├── run.py                            Entrypoint: `python run.py` → HTTP + WS on :8765
    ├── app.py                            App factory: blueprints, pages, background heartbeat monitor
    ├── config.py                         Every env-driven setting centralized (ports, timeouts, cooldowns)
    ├── extensions.py                     Shared flask-sock WebSocket instance
    ├── ai_router.py                      OpenRouter fallback chain: Gemini Flash → Groq Llama → Mistral → Qwen
    ├── robot_behavior.py                 AI text → validated robot command; builds Indonesian world-state context
    ├── robot_state.py                    Thread-safe telemetry store, event log, world snapshot, FSM transition log
    ├── requirements.txt                  flask, flask-sock, simple-websocket, requests, python-dotenv
    ├── .env.example                      Template → copy to .env (OPENROUTER_API_KEY etc.)
    ├── README.md                         Backend deep-dive: routes, AI routing, touch flow, tests
    ├── routes/
    │   ├── __init__.py                   Blueprint package marker
    │   ├── chat.py                       General AI chat features (independent of robot code)
    │   └── robot_ws.py                   /ws/robot WebSocket + /api/robot/* REST + MJPEG camera stream proxy
    ├── templates/
    │   ├── index.html                    Chat UI page
    │   └── dashboard.html                Developer dashboard page
    ├── static/
    │   ├── css/chat.css                  Chat styling
    │   ├── css/dashboard.css             Dashboard styling
    │   ├── js/chat.js                    Chat logic
    │   └── js/dashboard.js               Polling, panels, d-pad, speed slider, event simulation
    └── tests/
        ├── conftest.py                   Pytest fixtures (Flask test client)
        ├── test_routes.py                REST route tests (status/command/events/logs)
        ├── test_robot_behavior.py        Command building, validation, fallback vocabulary
        ├── test_v2.py                    V2: world context, world_state handling, V2 events, camera proxy
        └── run_e2e.py                    21-check E2E over a real WebSocket with a simulated ESP32
```

---

## Hardware

### Bill of Materials

| Part | Role | Notes |
|---|---|---|
| ESP32 DevKitC (ESP32-WROOM-32) | brain of the body | any standard 38-pin DevKit works |
| 2WD chassis + 2× TT DC motors | movement | with a caster wheel |
| TB6612FNG | dual motor driver | far more efficient than L298N |
| SSD1306 0.96″ I2C OLED 128×64 | the face | I2C address `0x3C` |
| TTP223 capacitive touch module | petting / wake | output HIGH while touched |
| Passive buzzer | beeps & jingles | LEDC-driven |
| WS2812B 8-LED strip | mood light | brightness capped in firmware |
| **V2:** HC-SR04 | obstacle distance | 5 V device — see divider warning |
| **V2:** MPU-6050 (GY-521) | orientation / tilt / motion | shares the OLED I2C bus at `0x68` |
| **V2:** AI-Thinker ESP32-CAM (OV2640) | companion vision unit | separate board, needs PSRAM |
| USB battery pack / Li-ion + regulator | power | motors on their own rail — see below |

### Pin map

Single source of truth: `esp32-firmware/src/config.h`. Nothing else in the
codebase hardcodes pins.

| Peripheral | Pin(s) | Notes |
|---|---|---|
| TB6612 STBY | GPIO 4 | active HIGH |
| TB6612 AIN1 / AIN2 / PWMA | GPIO 16 / 17 / 25 | left wheel (LEDC ch 0) |
| TB6612 BIN1 / BIN2 / PWMB | GPIO 18 / 19 / 26 | right wheel (LEDC ch 1) |
| OLED SDA / SCL | GPIO 21 / 22 | I2C `0x3C`, 400 kHz |
| TTP223 OUT | GPIO 32 | HIGH while touched |
| Buzzer | GPIO 27 | LEDC ch 2 |
| WS2812 DIN | GPIO 33 | 8 LEDs |
| **V2:** HC-SR04 TRIG | GPIO 23 | |
| **V2:** HC-SR04 ECHO | GPIO 34 | input-only; **5 V→3.3 V divider required** (e.g. 1 kΩ / 2 kΩ) |
| **V2:** MPU-6050 SDA / SCL | GPIO 21 / 22 | shares the OLED bus, addr `0x68` (AD0 low) |
| **V2:** Vision link | TCP **8766** | robot hosts the WS server; the camera unit connects in |

**Power safety**

- Motors draw from a proper battery pack through the TB6612 **VM** pin; the
  ESP32 is fed by its own regulated rail. Keep grounds common.
- Never wire raw Li-ion cells into the ESP32 3V3 pin.
- The HC-SR04 is a 5 V device — its ECHO pin **must** go through a voltage
  divider before GPIO 34 (which is input-only and has no protection).
- The firmware never leaves motors energized: every motion has a duration,
  a 1.5 s watchdog cap, and obstacle/fall/sleep/link-loss/FAULT all force STOP.

---

## Getting Started

### Prerequisites

- [VS Code](https://code.visualstudio.com/) + the
  [PlatformIO IDE extension](https://platformio.org/) (for firmware)
- Python **3.9+** (for the Local AI)
- A 2.4 GHz Wi-Fi network visible to the robot, camera unit and laptop
- An [OpenRouter](https://openrouter.ai/keys) API key *(optional — the whole
  system runs without it in safe-fallback mode)*

### 1 · Local AI (laptop)

```bash
cd local-ai
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env        # put your OPENROUTER_API_KEY in
.venv/bin/python run.py     # serves HTTP + WebSocket on :8765
```

- Chat UI:  <http://localhost:8765/>
- Dashboard: <http://localhost:8765/dashboard>

Without an API key everything still works — AI replies become a **safe
fallback command** (neutral face, blink, stop, friendly Indonesian speech),
so you can test the entire pipeline offline.

### 2 · ESP32 firmware (robot body)

```bash
cd esp32-firmware
cp include/secrets.h.example include/secrets.h   # then edit:
#   WIFI_SSID, WIFI_PASSWORD, AI_WS_HOST  (= your laptop's LAN IP)
pio run -t upload             # compile + flash
pio device monitor            # serial logs @115200
```

### 3 · ESP32-CAM unit (V2, optional but recommended)

```bash
cd esp32-cam-unit
cp include/secrets.h.example include/secrets.h   # then edit:
#   WIFI_SSID, WIFI_PASSWORD, ROBOT_HOST  (= the robot's LAN IP)
pio run -t upload             # flash (board: AI-Thinker ESP32-CAM)
pio device monitor
```

> ⚠️ **AI-Thinker boards have no USB chip.** Flash with an external
> 3.3 V USB-serial adapter: `U0R→TX`, `U0T→RX`, and connect **GPIO 0 → GND**
> to enter the bootloader, then press RST. Remove the IO0 jumper to boot
> normally afterwards.

The unit finds the robot automatically and starts reporting vision frames.
Its MJPEG preview is reachable on the LAN at `http://<cam-ip>/stream` — the
dashboard consumes it **through the Local AI proxy**, never browser-direct.

### 4 · Verify the link

- Robot OLED bottom-right dot lights up once the WebSocket is established.
- Dashboard STATUS shows `WS: ONLINE`; the VISION card shows the camera link.
- Walk in front of the camera → person detected → CURIOUS → orient → FOLLOW.
- Put your hand ~20 cm in front of the sonar → obstacle → STOP → inspect →
  back off → turn away.
- Tilt the robot past ~65° → FAULT, motors dead, *"Aduh!"* → stand it up
  for 1.5 s → recovers with a happy beep.

### Try it without any hardware

No robot built yet? You can still exercise the whole brain:

```bash
cd local-ai && .venv/bin/python -m pytest tests -q      # 51 unit tests
.venv/bin/python tests/run_e2e.py                       # 21-check E2E, simulated ESP32
cd ../esp32-firmware && bash test/test_protocol/run_native_tests.sh   # 134 checks
```

Then open the dashboard and use the **EVENT TEST** panel to inject simulated
`touch / person / person_lost / obstacle / obstacle_clear / fall / world_state`
events and watch the pipeline react.

---

## Configuration Reference

### `esp32-firmware/include/secrets.h` (git-ignored)

| Define | Meaning |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | 2.4 GHz network credentials |
| `AI_WS_HOST` | laptop LAN IP where Local AI runs, e.g. `"192.168.1.10"` |

### `esp32-cam-unit/include/secrets.h` (git-ignored)

| Define | Meaning |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | same network as the robot |
| `ROBOT_HOST` | robot's LAN IP (vision link target, port 8766) |

### `local-ai/.env`

| Variable | Default | Meaning |
|---|---|---|
| `OPENROUTER_API_KEY` | — | enables real AI replies (fallback mode without it) |
| `OPENROUTER_MODELS` | built-in chain | override the fallback chain, comma-separated |
| `PORT` | `8765` | must match `AI_WS_PORT` in the firmware `config.h` |
| `HOST` | `0.0.0.0` | bind address |
| `FLASK_DEBUG` | `0` | `1` enables Flask debug mode |
| `SECRET_KEY` | dev-only | Flask session secret |
| `AI_REQUEST_TIMEOUT_S` | `8` | per-provider OpenRouter timeout |
| `AI_MAX_TOKENS` | `150` | reply budget (speech is clipped to 128 chars) |
| `ROBOT_HEARTBEAT_TIMEOUT_S` | `8` | silence before the robot counts as offline |
| `ROBOT_SPEED_MIN_PWM` / `ROBOT_SPEED_MAX_PWM` | `60` / `255` | dashboard speed-slider mapping window |
| `CAMERA_STREAM_PORT` | `80` | MJPEG port served by the camera unit |
| `CAMERA_STREAM_TIMEOUT_S` | `5` | proxy connect/read timeout |
| `CAMERA_STREAM_CHUNK` | `4096` | proxy chunk size |
| `ROBOT_PERSON_GREET_COOLDOWN_S` | `45` | min seconds between AI greetings per person reappearance |

### Firmware tunables (`esp32-firmware/src/config.h`)

All thresholds live in clearly-labelled V2 blocks — no magic numbers anywhere:

| Group | Key values |
|---|---|
| Sonar | sample 66 ms (~15 Hz) · echo timeout 25 ms (~4.3 m) · median-of-3 + EMA 40 % · valid ≤ 200 cm · stale 600 ms |
| Obstacle | hysteresis enter **25 cm** / clear **32 cm** · emergency CLOSE **12 cm** |
| IMU | 50 Hz sampling · angle EMA 15 % · upright ≤ 25° · fall tilt ≥ 65° (400 ms) · inverted ≥ 100° · still ≤ 20°/s · shock 2.4 g |
| Vision | link port 8766 · link timeout 3 s · person flag fresh 1.5 s · confirm 2 frames · lost after 2 s |
| Behavior V2 | CURIOUS 2.5 s · SURPRISED 1.2 s · SEARCH 5 s · INTERACT 3.5 s · FOLLOW step 700 ms / max 10 s / cooldown 8 s / keep-out 20 cm · interact < 60 cm · FAULT recovery 1.5 s |
| Camera unit | GRAYSCALE QVGA 320×240 · detection every 150 ms · 3 frames confirm / 12 release · center band 30 % · stream 1 client max |

---

## Using the Robot & Dashboard

### What the robot does day-to-day

- **Boots** with a startup jingle, announces `boot_completed` once, greets you.
- **Idles alive:** blinks, glances, micro-moves and chirps on randomized
  timers; after 30–60 s of nothing it **sleeps** (wake: touch or AI command).
- **Sees you:** person appears → CURIOUS (turns toward the zone) → follows
  at a polite distance → INTERACT when you come within 60 cm → SEARCH
  (looks left/right) when you vanish → back to IDLE after 5 s.
- **Feels obstacles:** obstacle < 25 cm → stop, inspect left/right, small
  back-off, turn away. Closer than 12 cm → SURPRISED first. Idle wandering
  and exploring are suppressed while an obstacle is known.
- **Knows when it falls:** FAULT — motors dead, error LEDs, *"Aduh!"* —
  until it has been upright again for 1.5 s, then a happy recovery beep.
- **Petted:** touch → THINKING → the AI answers with a face, an animation,
  a movement and Indonesian speech.

### Dashboard panels (`/dashboard`, auto-refresh 2 s)

| Panel | Contents |
|---|---|
| STATUS | link state, behavior FSM, emotion, uptime, RSSI, brain/provider |
| VISION | camera link, person detected, target zone, **live MJPEG stream** (via proxy) |
| DISTANCE | filtered distance (cm) + obstacle / CLOSE badges |
| IMU | orientation (UPRIGHT / TILTED / FALLEN), motion level, sensor health |
| WORLD STATE | one-line perception summary, updated from `world_state` |
| MOVEMENT | d-pad + speed slider (0–100 % → PWM 60–255) |
| EMOTION / ANIMATION TEST | 6 emotions, 5 one-shot animations |
| EVENT TEST | simulate `touch / person / person_lost / obstacle / obstacle_clear / fall / world_state` |
| LOGS | color-coded stream with filters (now including FSM `state` transitions) |

Traffic path is strictly **Browser → Local AI → (WS/proxy) → robot/camera** —
the browser never talks to the robot or camera directly.

---

## Communication Protocol

All JSON, strictly whitelist-validated on both ends. Malformed or unknown
messages are logged and ignored — bad input can never crash the robot.
V2 additions are purely additive: V1 consumers can ignore them safely.

### REST endpoints (Local AI)

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | chat UI |
| `/dashboard` | GET | developer dashboard |
| `/api/health` | GET | liveness probe |
| `/api/chat` | POST | general chat `{message}` → `{reply, provider}` |
| `/ws/robot` | WS | the ESP32 connection |
| `/api/robot/status` | GET | full robot + brain + world snapshot |
| `/api/robot/command` | POST | validated manual command (d-pad, emotions, …) |
| `/api/robot/event` | POST | simulated events (dashboard EVENT TEST) |
| `/api/robot/logs` | GET | event/command/error log stream |
| `/api/robot/camera/stream` | GET | MJPEG proxy to the camera unit (503 when offline) |

### ESP32 → Local AI (events)

```json
{"event": "touch_head"}
{"event": "boot_completed"}
{"event": "heartbeat", "uptime_s": 120, "rssi": -52, "state": "IDLE", "emotion": "neutral"}
{"event": "person_detected", "zone": "left"}
{"event": "person_lost"}
{"event": "obstacle_detected"}
{"event": "obstacle_cleared"}
{"event": "fall_detected"}
{"event": "world_state", "person": true, "zone": "center", "distance": 45.5,
 "obstacle": false, "close": false, "upright": true, "motion": "moving",
 "state": "FOLLOW", "vision": true}
```

`world_state` is broadcast every 5 s. Heartbeats fly every 3 s; 5 s of
silence flips the robot to OFFLINE (its own FSM, not the AI's opinion).

### Local AI → ESP32 (commands)

| Field | Values | Notes |
|---|---|---|
| `emotion` | `neutral` `happy` `curious` `sleepy` `thinking` `scared` | face change |
| `animation` | `smile` `blink` `look_left` `look_right` `tilt` | one-shot overlay |
| `movement` | `forward_small` `backward_small` `turn_left` `turn_right` `stop` | duration-capped |
| `speech` | any text | clipped to 128 chars, ASCII-sanitized on the OLED |
| `speed` | optional 1–255 | clamped PWM override (dashboard slider maps % → 60–255) |
| `duration_ms` | optional | clamped to firmware limits, watchdog still applies |

### Camera unit → Robot (vision link, :8766/ws/vision)

```json
{"type": "vision", "person": true, "zone": "center", "score": 42}
```

`person` is mandatory, `zone` optional, `score` is raw motion energy
(0–255, clamped) — useful for tuning on the bench. Frames are sent every
500 ms or on change; malformed frames are dropped.

---

## Behavior FSM (13 states)

V1 states and transitions are preserved exactly; V2 adds seven.

| State | Trigger | What it does | Exit |
|---|---|---|---|
| `BOOTING` | power-on | startup face + jingle | 2.5 s → IDLE |
| `IDLE` | default | life signs: blink/glance/micro-move/chirp | sleep, explore, or events |
| `EXPLORE` | idle personality | 6 bounded steps over ~8 s | done → IDLE |
| `THINKING` | touch (online) | thinking face, awaits AI reply | 3 s timeout → fallback |
| `HAPPY` | touch (offline) / celebration | happy face + sound | 2 s → IDLE |
| `SLEEP` | 30–60 s idle | sleepy face, LEDs dim | touch or AI command |
| `CURIOUS` 🆕 | person detected | curious face, tilt, beep, turn toward zone | near → INTERACT; present → FOLLOW; else IDLE |
| `INTERACT` 🆕 | person < 60 cm | happy face, smile, happy sound | refreshes while present; lost → SEARCH |
| `FOLLOW` 🆕 | person + zone (cooldown-gated) | paced hops toward the zone — never closer than 20 cm, max 10 s per chase | lost → SEARCH; obstacle → AVOID; bound → INTERACT |
| `SEARCH` 🆕 | person lost | scans left / right | re-found → CURIOUS; 5 s → IDLE |
| `AVOID_OBSTACLE` 🆕 | obstacle active | stop → inspect L/R → small back-off → turn away | ~1.5 s timeline → IDLE |
| `SURPRISED` 🆕 | obstacle CLOSE (< 12 cm) | scared face, error sound, small hop back | 1.2 s → AVOID_OBSTACLE |
| `FAULT` 🆕 | fall latched (IMU) | **motors dead**, error LEDs, *"Aduh!"* | upright 1.5 s → IDLE + happy beep |

**Priority:** FAULT > SURPRISED / AVOID > touch / THINKING > person states >
EXPLORE > IDLE > SLEEP.

The Behavior FSM and Connection FSM remain fully separate — losing Wi-Fi
never stops the personality, and behavior never touches the transport.

---

## Perception Pipeline

```
HC-SR04 ──┐
MPU-6050 ─┼─> PerceptionEngine ──> WorldState ──> Behavior FSM ──> actuators
ESP32-CAM ┘   filter · debounce ·     shared single      13 states
              hysteresis · events     belief struct
```

- **Person presence** needs 2 confirming vision frames and is dropped after
  2 s of silence — brief occlusions and single noisy frames don't flap.
- **Obstacle flags** use hysteresis (enter < 25 cm, clear > 32 cm) so
  borderline objects can't dither the flag; < 12 cm is a separate CLOSE level.
- **Falls latch** when tilt ≥ 65° persists 400 ms (instantly when inverted
  past 100°) and clear only after 1.5 s upright — supervised, not edge-triggered.
- **Sonar filtering:** median-of-3 then EMA at ~15 Hz; readings older than
  600 ms count as "no data" instead of a stale guess.
- **Event queue:** perception emits into a small FIFO; behavior consumes it
  on its own tick — no sensor ever blocks the loop.
- **Deterministic guards in `main.cpp`:** fall or close obstacle ⇒
  `motors.stop()` immediately, before any FSM or AI logic runs.

The AI side stays honest too: `person_detected` triggers a greeting (45 s
cooldown so a wandering pet doesn't burn API quota), `touch_head` and AI
prompts carry an Indonesian **world-state context line** ("*Konteks persepsi
robot: Ada orang terdeteksi di arah kanan; jarak sekitar 40 cm (dekat)…*"),
and `fall_detected` is only logged — recovery is deterministic on the ESP32,
because a fallen pet must not wait on an API roundtrip.

---

## Testing & Verification

### Automated (all runnable without hardware)

| Suite | Command | Status |
|---|---|---|
| Native protocol tests (C++, 134 checks) | `bash test/test_protocol/run_native_tests.sh` in `esp32-firmware/` | ✅ 134/134 |
| Python unit tests (51) | `.venv/bin/python -m pytest tests -q` in `local-ai/` | ✅ 51/51 |
| E2E over real WebSocket (21 checks) | `.venv/bin/python tests/run_e2e.py` in `local-ai/` | ✅ 21/21 |
| Firmware compile, robot (`esp32dev`) | `pio run -e esp32dev` | ✅ 0 warnings, RAM 15.0 %, Flash 76.1 % |
| Firmware compile, camera (`esp32cam`) | `pio run` in `esp32-cam-unit/` | ✅ 0 warnings, RAM 18.2 %, Flash 31.8 % |

The E2E suite boots the real Flask app, connects a simulated ESP32 over a
genuine WebSocket and verifies: telemetry, boot greeting, touch → AI →
command, safe fallback on AI failure, malformed-JSON immunity, V2 events
(person/obstacle_cleared/fall/world_state), camera-proxy degradation and
disconnect handling.

### Manual hardware procedures

`docs/TESTING.md` defines bench procedures **M0–M15** — wiring check, boot,
outputs, motors & auto-stop, touch, sleep/wake, connection FSM, AI flow,
offline personality, dashboard E2E, plus the V2 set: **M10** sonar, **M11**
IMU, **M12** vision link, **M13** curious/follow/interact, **M14** obstacle
avoidance, **M15** fall/fault recovery — ending in a final acceptance
checklist for both V1 core and V2 perception.

> **Honest scope note.** Person detection is a **motion-energy baseline**
> (frame differencing with zoning), not identity-aware detection — a silent,
> motionless person is invisible to it. The detector sits behind the
> `IDetector` interface; an ESP-WHO face-detection backend is the documented
> swap-in path. Sensor behaviors (M10–M15) require on-hardware verification
> before being claimed as working — nothing here pretends otherwise.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| OLED stays black | check I2C address `0x3C`, SDA=21 / SCL=22 wiring |
| Motors never move | STBY must be HIGH; check VM battery; PWM pins 25/26 |
| Robot can't join Wi-Fi | verify `include/secrets.h`; ESP32 is **2.4 GHz only** |
| WS never connects | laptop firewall on 8765; `AI_WS_HOST` = laptop LAN IP; server running |
| Dashboard shows `NO API KEY` | fill `OPENROUTER_API_KEY` in `local-ai/.env`, restart |
| Motors creep after a command | update firmware — motion deadlines are mandatory |
| **V2:** distance shows "no data" | TRIG=23 / ECHO=34 wiring; **check the 5 V→3.3 V divider** on ECHO |
| **V2:** IMU unhealthy | MPU-6050 addr `0x68`; OLED + IMU share the bus — check address jumpers |
| **V2:** VISION card OFFLINE | camera powered? `ROBOT_HOST` = robot IP in cam `secrets.h`? robot on Wi-Fi? |
| **V2:** stream black in dashboard | test `http://<cam-ip>/stream` on the LAN first; proxy only allows 1 client |
| **V2:** camera unit won't flash | GPIO 0 → GND during reset; use a 3.3 V adapter (5 V adapters can brick the board) |
| **V2:** person never detected | wave a hand in view — the detector is motion-based; tune `DIFF_THRESHOLD` in `MotionZoneDetector.cpp` |
| **V2:** robot doesn't FOLLOW | FOLLOW is cooldown-gated (8 s) and needs a zone; check WORLD STATE panel |

---

## Extending the Project

- **Better vision:** implement `IDetector`
  (`esp32-cam-unit/src/detect/Detector.h`) — e.g. ESP-WHO face detection —
  and swap it in `main.cpp`. One file, nothing else changes.
- **Different IMU:** the driver sits behind `ImuSensor`
  (`esp32-firmware/src/imu/imu.h`). Replace `imu.cpp`, keep the interface.
- **New sensor:** new `src/<sensor>/` folder (header + impl), register its
  event in `protocol/`, feed it into `perception/` — never directly into
  `behavior/`. No changes to `connection/` required.
- **Different LLM provider:** edit the chain in `local-ai/config.py`
  (`OPENROUTER_MODELS`) — any OpenRouter model works.
- **Dashboard widgets:** panels read `/api/robot/status`; add a card in
  `templates/dashboard.html` + `static/js/dashboard.js`.

---

## Design Principles

1. **Non-blocking everything** — `millis()` deadlines, FSMs and timers;
   no `delay()` in the Arduino loop, ever.
2. **`main.cpp` only orchestrates** — modules own their logic; the safety
   guards in `main.cpp` are deliberately dumb and deterministic.
3. **One config file** — all pins/timings/thresholds in `config.h`
   (firmware) and `config.py` (server). No magic numbers in modules.
4. **Protocol is host-testable** — `src/protocol/` compiles without
   Arduino.h, so the entire wire vocabulary is unit-tested on a PC.
5. **FSMs stay separated** — behavior vs. connection vs. camera Wi-Fi mini-FSM.
6. **AI is a suggestion engine** — strict validation, duration caps,
   watchdogs, obstacle forward-block and FAULT lock outrank any command.
7. **Honesty in docs** — hardware behaviors not verified on the bench are
   listed as procedures to run, never as "done".

---

## Roadmap

**Deliberately out of scope for V2:** SLAM, advanced autonomous navigation,
identity-aware face recognition, voice recognition / synthesis, large local
LLMs, cloud robotics, multi-user infrastructure, OTA updates.

**Natural V3 candidates** (the V2 architecture keeps these clean):
ESP-WHO face detection behind `IDetector`, voice I/O, OTA firmware updates,
on-robot battery/voltage telemetry, and richer world models (multiple
tracked people, memory of favorite spots).

---

## Documentation Map

| Document | Contents |
|---|---|
| `README.md` (this file) | architecture, hardware, setup, protocol, FSMs, testing |
| `esp32-firmware/README.md` | firmware module map, log tags, build stats, sensor recipe |
| `esp32-cam-unit/README.md` | camera unit design, detection scope, tuning knobs |
| `local-ai/README.md` | backend routes, AI routing, touch flow, production notes |
| `docs/TESTING.md` | automated suites + manual procedures M0–M15 + acceptance checklist |

---

## Built With

[PlatformIO](https://platformio.org/) ·
[ArduinoJson](https://github.com/bblanchon/ArduinoJson) ·
[arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) ·
[Adafruit SSD1306 / GFX / NeoPixel](https://www.adafruit.com/) ·
[Flask](https://flask.palletsprojects.com/) ·
[flask-sock](https://github.com/dhdaines/flask-sock) ·
[OpenRouter](https://openrouter.ai/)

## License

Personal DIY project — no license applied yet. If you fork or publish a
derivative, adding an explicit license (MIT is a good default) and crediting
the inspiration is encouraged.
