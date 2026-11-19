<div align="center">

# Testing Guide

**Verification procedures for the AI Pet Robot**

Automated suites that run anywhere, and bench procedures for the real hardware.

<br>

![Automated checks](https://img.shields.io/badge/automated_checks-206-2e7d32?style=flat-square)
![Manual procedures](https://img.shields.io/badge/manual_procedures-16-2f3640?style=flat-square)
![Hardware required](https://img.shields.io/badge/hardware-only_for_M0--M15-2f3640?style=flat-square)

[Automated tests](#automated-tests) &nbsp;·&nbsp;
[Core procedures](#core-procedures-m0m9) &nbsp;·&nbsp;
[Perception procedures](#perception-procedures-m10m15) &nbsp;·&nbsp;
[Acceptance checklist](#acceptance-checklist)

</div>

<br>

## Overview

Testing is split into two layers.

| Layer | Needs hardware | What it proves |
|---|---|---|
| **Automated** | No | Protocol, validation, fallback, routes, world state and the full WebSocket round trip |
| **Manual bench** | Yes | Motors, sensors, timing, FSM behavior and everything physical |

Run the automated layer first. Then work through the bench procedures with the real robot, ticking each box as you go. M13 depends on M12.

### Procedure index

| ID | Procedure | Area |
|---|---|---|
| M0 | Bench wiring check | Core |
| M1 | Boot | Core |
| M2 | Outputs | Core |
| M3 | Motors and auto-stop | Core |
| M4 | Touch and debounce | Core |
| M5 | Sleep / wake | Core |
| M6 | Connection FSM | Core |
| M7 | AI flow (online) | Core |
| M8 | Offline personality | Core |
| M9 | Dashboard end-to-end | Core |
| M10 | HC-SR04 distance | Perception |
| M11 | IMU orientation | Perception |
| M12 | Vision link and camera unit | Perception |
| M13 | Curious / follow / interact | Perception |
| M14 | Obstacle avoidance | Perception |
| M15 | Fall / fault recovery | Perception |

<br>

## Automated tests

| Suite | Command | Scope |
|---|---|---|
| **Python unit** (51 tests) | `cd local-ai && .venv/bin/python -m pytest tests -q` | Command validation, fallback, routes, speed mapping, chat API, V2 world state and context |
| **E2E** (21 checks) | `.venv/bin/python tests/run_e2e.py` | Real WebSocket round trip with a simulated ESP32, including the V2 perception pipeline |
| **Native C++** (134 checks) | `cd esp32-firmware && bash test/test_protocol/run_native_tests.sh` | Protocol tables, serialization, hostile JSON, vision frames |
| **Firmware build** | `pio run -e esp32dev` (robot) · `pio run` in `esp32-cam-unit/` | Full compile of both targets |

### Coverage map

What the automated layer covers, and which bench procedure picks up the rest.

| Area | Automated coverage | Bench procedure |
|---|---|---|
| **Protocol parsing and invalid JSON** | Tested on both sides (C++ native and Python): truncated payloads, wrong types, unknown values, oversized speech, speed/duration clamping. V2 adds vision-frame hostility: missing `person`, non-bool `person`, score clamping. | n/a |
| **Motor safety** | Duration clamping and auto-stop deadlines are firmware-internal. The E2E suite verifies the command contract: `movement` is always bounded and `stop` is always honored. | M3 |
| **Touch debounce** | Firmware-timed (60 ms), not covered automatically. | M4 |
| **FSM transitions** | Behavior and connection transitions are exercised on hardware. | M6–M8, and M13–M15 for perception-driven states |
| **World state (V2)** | `mark_world_state` whitelist, typing and FSM logging, plus the AI world-context renderer, are unit-tested. E2E drives a full `world_state` → snapshot → `behavior_state` round trip. | n/a |

<br>

## Core procedures (M0–M9)

### M0 · Bench wiring check

> [!WARNING]
> Power off while wiring. Never connect USB while the motor battery is on VM.

- [ ] USB is not connected while the motor battery is on VM.
- [ ] TB6612 VM comes from the battery pack, VCC from ESP32 3V3, grounds common.
- [ ] OLED at `0x3C` on SDA 21 / SCL 22; WS2812 DIN on GPIO 33 through 330 Ω.
- [ ] **V2:** HC-SR04 VCC on 5 V, TRIG on GPIO 23, ECHO through a **5 V → 3.3 V divider** into GPIO 34.
- [ ] **V2:** MPU-6050 on SDA 21 / SCL 22 (AD0 low → `0x68`). It coexists with the OLED on the same bus.
- [ ] **V2:** ESP32-CAM unit flashed with its own `secrets.h` (`ROBOT_HOST` = the robot's LAN IP).

### M1 · Boot

- [ ] ESP32 boots and serial shows `[PetRobotV2 v2.0.0] booting...`
- [ ] OLED shows the splash `PetRobotV2 v2.0.0`, then the neutral face.
- [ ] **V2:** serial shows `[sonar] HC-SR04 ready` and `[imu] MPU-6050 ready`, or a graceful "not responding" while the robot still boots.

> [!NOTE]
> Missing sensors must never brick the robot. A "not responding" message with a normal boot is a pass.

### M2 · Outputs

- [ ] Startup jingle plays (C-E-G-C rising).
- [ ] LEDs pulse in the default cyan.

### M3 · Motors and auto-stop

- [ ] Dashboard ▲: robot nudges forward about 0.35 s and **stops by itself**.
- [ ] ◀ / ▶ turn in place; ■ stops instantly mid-motion.
- [ ] Speed slider changes nudge strength.
- [ ] Unplug Wi-Fi mid-motion: motors stop within about 5 s (link-loss guard).

### M4 · Touch and debounce

- [ ] One clean tap gives exactly one reaction (THINKING or HAPPY).
- [ ] Rapid rattling taps do not machine-gun events; the debounce holds.

### M5 · Sleep / wake

- [ ] Leave idle for 30–60 s: sleepy face, LEDs off, quiet.
- [ ] Touch wakes it to IDLE with a beep and a blink.

### M6 · Connection FSM

- [ ] Boot with the laptop down: logs show CONNECTING → OFFLINE → RECONNECTING with 5 / 10 / 20 / 40 / 60 s gaps (watch serial).
- [ ] Start Local AI mid-backoff: the robot joins **without rebooting**.
- [ ] Kill Local AI for 30 s and restart: the robot re-joins by itself.

### M7 · AI flow (online)

- [ ] Touch the robot: THINKING face, blue breathing LED, thinking blips.
- [ ] Within about 3 s an AI command arrives (HAPPY or curious face, speech on the OLED bar, possibly a small move). Dashboard LOGS show the full exchange with the provider name.
- [ ] Remove `OPENROUTER_API_KEY`, restart, touch again: fallback speech arrives instead. Nothing hangs, nothing crashes.

### M8 · Offline personality

- [ ] With Wi-Fi off, the robot still blinks, glances and chirps, occasionally does a curious wiggle-walk (EXPLORE), and goes HAPPY when touched.

### M9 · Dashboard end-to-end

- [ ] STATUS values move (RSSI, uptime, state).
- [ ] Every EMOTION / ANIMATION button visibly changes the robot.
- [ ] EVENT TEST buttons inject events that appear in LOGS.
- [ ] **V2:** the new EVENT TEST buttons (person / person lost / obstacle clear / fall / world state) appear in LOGS and update the V2 cards.
- [ ] The existing AI chat at `/` still answers, with the provider shown.

<br>

## Perception procedures (M10–M15)

### M10 · HC-SR04 distance

- [ ] The DISTANCE card shows a plausible cm value. Slide your hand closer and the value follows smoothly (median + EMA filtering, no wild jumps).
- [ ] Object at about 25 cm: obstacle badge **YES**. Pull back past about 32 cm: **CLEAR**. Hysteresis holds, no flapping near the threshold.
- [ ] Object at about 12 cm: `CLOSE!` badge, and the robot reacts (see M14).
- [ ] Unplug the sonar: "no data" appears and the robot keeps working.

### M11 · IMU orientation

- [ ] The IMU card shows UPRIGHT. Tilt the robot about 40°: TILTED.
- [ ] Lay it on its back: INVERTED, and the dashboard flips to TILTED / FALLEN.
- [ ] Shake gently: motion `moving`. Sharp bump: `shock`, then still.
- [ ] Unplug the IMU: the card shows unhealthy / still and the robot keeps working.

### M12 · Vision link and camera unit

- [ ] Camera unit boots and joins Wi-Fi; robot serial prints `[vision] camera unit connected from <ip>`.
- [ ] The VISION card flips to ONLINE. Person and zone update as you move in front of the lens.
- [ ] `http://<cam-ip>/stream` shows the raw MJPEG stream on the LAN.
- [ ] The dashboard shows the proxied stream (Browser → Local AI → camera).
- [ ] Power the camera off: the VISION card goes OFFLINE within about 3 s and the robot stays fully functional. Vision loss is not a crash.

### M13 · Curious / follow / interact

*Depends on M12.*

- [ ] Enter the view: the robot gets CURIOUS (face, tilt, beep) and turns toward your zone.
- [ ] Stay close (< 60 cm): INTERACT, with a happy face and a happy sound.
- [ ] Step back and move sideways: FOLLOW. Small paced hops toward you, never closer than about 20 cm. The chase is bounded (about 10 s), then cools down.
- [ ] Leave the view: SEARCH scans left / right for about 5 s, then IDLE.
- [ ] AI greeting: with a key configured, `person_detected` triggers a greeting command, at most once per 45 s. Check LOGS for the cooldown message.

### M14 · Obstacle avoidance

- [ ] While the robot explores or idles, put an object about 15 cm ahead: it stops, looks left / right, backs off a little, turns away and returns to IDLE. Motors never push into the object.
- [ ] Close approach (< 12 cm): SURPRISED (scared face, error sound, small hop back), then the avoid routine.
- [ ] Dashboard ▲ with an obstacle inside the sonar cone: the forward command is rejected (STOP, `forward blocked` in serial).
- [ ] AI command `forward_small` with an obstacle present: same rejection. The AI cannot drive the robot into the obstacle.

### M15 · Fall / fault recovery

- [ ] Tilt the robot past about 65° for under a second: no fault (debounce).
- [ ] Lay it down: motors stop immediately, FAULT state, error LED, "Aduh!" on the OLED. Dashboard IMU and LOGS agree.
- [ ] Any AI movement command during FAULT is ignored (movement lock).
- [ ] Set it upright again: after about 1.5 s it recovers to IDLE with a happy beep.

<br>

## Acceptance checklist

The final sign-off. Every box should be ticked before a build is called working.

### Core

**Hardware and outputs**

- [ ] ESP32 boots
- [ ] OLED works
- [ ] Motors work
- [ ] Stop works
- [ ] Touch works
- [ ] Debounce works
- [ ] Buzzer works
- [ ] WS2812 works

**Personality**

- [ ] Local personality works
- [ ] Sleep / wake works

**Connectivity and safety**

- [ ] Wi-Fi connects
- [ ] WebSocket connects
- [ ] ESP32 sends events
- [ ] Local AI receives events
- [ ] Local AI sends commands
- [ ] ESP32 executes commands
- [ ] Movement auto-stops
- [ ] Invalid JSON is safe
- [ ] Offline mode works
- [ ] Reconnect works without reboot

**Dashboard and AI**

- [ ] Dashboard works
- [ ] Dashboard controls movement
- [ ] Dashboard tests emotions / animations
- [ ] Dashboard logs events
- [ ] Touch → AI → robot response works
- [ ] AI failure fallback works
- [ ] Existing Local AI features still work

### Perception

**Vision**

- [ ] ESP32-CAM unit boots and links to the robot
- [ ] Camera stream visible (LAN) and proxied in the dashboard
- [ ] Basic person detection works (motion baseline, M12 / M13)
- [ ] Target direction (zone) works

**Distance and obstacles**

- [ ] HC-SR04 works, filtered distance is stable
- [ ] Obstacle detection works (hysteresis holds)
- [ ] Obstacle avoidance works (stop → inspect → back off → turn)
- [ ] Close-obstacle SURPRISED works

**IMU and fault handling**

- [ ] IMU works (orientation, motion, health)
- [ ] Fall latched → FAULT: motors dead, error output
- [ ] Recovery: upright grace → IDLE + beep

**Behavior**

- [ ] CURIOUS works
- [ ] FOLLOW works (paced, bounded, cooldown)
- [ ] SEARCH works
- [ ] INTERACT works

**Fusion and telemetry**

- [ ] Perception layer fuses sensors (no direct sensor → behavior wiring)
- [ ] `world_state` reaches the dashboard and the AI context
- [ ] Local AI receives useful perception data
- [ ] Dashboard exposes V2 telemetry (VISION / DISTANCE / IMU / WORLD)

**Protocol and resilience**

- [ ] Protocol updated safely (V1 messages still accepted)
- [ ] Offline safety behavior works (vision loss / link loss = safe)
- [ ] Failure cases handled (sonar / IMU / camera unplugged = no crash)
- [ ] Sensor-absent boot works (robot functional without V2 hardware)

<br>

> [!IMPORTANT]
> **Verification status.** Automated portions are verified in this repo: protocol parsing and vision frames, invalid JSON safety, fallback pipeline, `world_state` telemetry and AI context, dashboard command delivery, the E2E WebSocket round trip, and both firmware compilations. Hardware-dependent behaviors (M3–M15) require bench verification before being claimed as working.
