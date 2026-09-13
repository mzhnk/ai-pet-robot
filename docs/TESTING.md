# Testing Guide — AI Pet Robot V2

Two layers:

1. **Automated** (runs anywhere, no hardware)
2. **Manual hardware procedures** (bench test with the real robot)

---

## 1. Automated tests

| Suite | Command | Scope |
|---|---|---|
| Python unit | `cd local-ai && .venv/bin/python -m pytest tests -q` | command validation, fallback, routes, speed mapping, chat API, V2 world state + context (51 tests) |
| E2E | `.venv/bin/python tests/run_e2e.py` | real WebSocket round-trip with a simulated ESP32, incl. V2 perception pipeline (21 checks) |
| Native C++ | `cd esp32-firmware && bash test/test_protocol/run_native_tests.sh` | protocol tables, serialization, hostile JSON, vision frames (134 checks) |
| Firmware build | `pio run -e esp32dev` (robot) · `pio run` (esp32-cam-unit) | full compile of both targets |

Automated coverage notes:
- **Protocol parsing & invalid JSON** — both sides tested (C++ native +
  Python), including truncated payloads, wrong types, unknown values,
  oversized speech, speed/duration clamping, and (V2) vision-frame
  hostility (missing `person`, non-bool person, score clamping).
- **Motor safety (software part)** — duration clamping and auto-stop
  deadlines are firmware-internal; the E2E suite verifies the command
  contract (`movement` always bounded, `stop` always honored). The
  physical behavior is covered by procedure M3.
- **Touch debounce** — firmware-timed (60 ms); verified in procedure M4.
- **FSM transitions** — behavior/connection transitions are exercised in
  hardware by procedures M6–M8; V2 perception-driven states by M13–M15.
- **World state (V2)** — `mark_world_state` whitelist/typing/FSM-logging
  and the AI world-context renderer are unit-tested; the E2E suite drives
  a full world_state → snapshot → behavior_state round trip.

---

## 2. Manual hardware procedures

### M0 — Bench wiring check (power off!)
- [ ] USB not connected while motor battery is on VM.
- [ ] TB6612 VM from battery pack, VCC from ESP32 3V3, grounds common.
- [ ] OLED at 0x3C on SDA21/SCL22; WS2812 DIN on GPIO33 via 330 Ω.
- [ ] V2: HC-SR04 VCC 5V, TRIG on GPIO23, ECHO through a **5V→3.3 V
      divider** into GPIO34.
- [ ] V2: MPU-6050 on SDA21/SCL22 (AD0 low → 0x68); it coexists with the
      OLED on the same bus.
- [ ] V2: ESP32-CAM unit flashed with its own `secrets.h` (ROBOT_HOST =
      the robot's LAN IP).

### M1 — Boot
- [ ] ESP32 boots, serial shows `[PetRobotV2 v2.0.0] booting...`
- [ ] OLED splash `PetRobotV2 v2.0.0`, then neutral face.
- [ ] V2: serial shows `[sonar] HC-SR04 ready` and
      `[imu] MPU-6050 ready` (or a graceful “not responding” + the robot
      still boots — missing sensors must not brick it).

### M2 — Outputs
- [ ] Startup jingle plays (C-E-G-C rising).
- [ ] LEDs pulse in the default cyan.

### M3 — Motors & auto-stop
- [ ] Dashboard ▲: robot nudges forward ~0.35 s and **stops by itself**.
- [ ] ◀/▶ turn in place; ■ stops instantly mid-motion.
- [ ] Speed slider changes nudge strength.
- [ ] Unplug Wi-Fi mid-motion → motors stop within ~5 s (link-loss guard).

### M4 — Touch & debounce
- [ ] One clean tap → exactly one reaction (THINKING or HAPPY).
- [ ] Rapid rattling taps do not machine-gun events (debounce holds).

### M5 — Sleep / wake
- [ ] Leave idle 30–60 s → sleepy face, LEDs off, quiet.
- [ ] Touch → wakes to IDLE with a beep and blink.

### M6 — Connection FSM
- [ ] Boot with laptop down: logs show CONNECTING → OFFLINE →
      RECONNECTING with 5/10/20/40/60 s gaps (watch serial).
- [ ] Start Local AI mid-backoff: robot joins **without rebooting**.
- [ ] Kill Local AI for 30 s and restart: robot re-joins by itself.

### M7 — AI flow (online)
- [ ] Touch robot: THINKING face + blue breathing LED + thinking blips.
- [ ] Within ~3 s: AI command arrives (HAPPY or curious face, speech on
      the OLED bar, possibly a small move). Dashboard LOGS show the full
      exchange with the provider name.
- [ ] Remove `OPENROUTER_API_KEY`, restart, touch again → fallback
      speech arrives instead (nothing hangs, nothing crashes).

### M8 — Offline personality
- [ ] With Wi-Fi off: robot still blinks, glances, chirps, occasionally
      does a curious wiggle-walk (EXPLORE), and goes HAPPY when touched.

### M9 — Dashboard end-to-end
- [ ] STATUS values move (RSSI, uptime, state).
- [ ] Every EMOTION/ANIMATION button visibly changes the robot.
- [ ] EVENT TEST buttons inject events that appear in LOGS.
- [ ] V2: new EVENT TEST buttons (person / person lost / obstacle clear /
      fall / world state) appear in LOGS and update the V2 cards.
- [ ] Existing AI chat at `/` still answers (provider shown).

### M10 — HC-SR04 distance (V2)
- [ ] Dashboard DISTANCE card shows a plausible cm value; slide your hand
      closer → value follows smoothly (median+EMA filtering, no wild jumps).
- [ ] Object at ~25 cm → obstacle badge YES; pull back past ~32 cm → CLEAR
      (hysteresis, no flapping near the threshold).
- [ ] Object at ~12 cm → `CLOSE!` badge and the robot reacts (M13).
- [ ] Unplug the sonar → "no data" appears; robot keeps working.

### M11 — IMU orientation (V2)
- [ ] Dashboard IMU card shows UPRIGHT; tilt the robot ~40° → TILTED.
- [ ] Lay it on its back → INVERTED; dashboard flips to TILTED/FALLEN.
- [ ] Shake gently → motion `moving`; sharp bump → `shock` (then still).
- [ ] Unplug the IMU → card shows unhealthy/still; robot keeps working.

### M12 — Vision link & camera unit (V2)
- [ ] Camera unit boots, joins Wi-Fi, robot serial prints
      `[vision] camera unit connected from <ip>`.
- [ ] Dashboard VISION card flips to ONLINE; person/zone update when you
      move in front of the lens.
- [ ] `http://<cam-ip>/stream` shows the raw MJPEG stream on the LAN.
- [ ] Dashboard shows the proxied stream (Browser → Local AI → camera).
- [ ] Power the camera off → VISION card → OFFLINE within ~3 s; robot
      stays fully functional (vision loss is not a crash).

### M13 — Curious / follow / interact (V2, needs M12)
- [ ] Enter the view: robot gets CURIOUS (face + tilt + beep) and turns
      toward your zone.
- [ ] Stay close (< 60 cm): INTERACT — happy face, happy sound.
- [ ] Step back and move sideways: FOLLOW — small paced hops toward you,
      never closer than ~20 cm; chase is bounded (~10 s) then cools down.
- [ ] Leave the view: SEARCH — scans left/right ~5 s → IDLE.
- [ ] AI greeting: with a key configured, `person_detected` triggers a
      greeting command (at most once per 45 s — check LOGS for the
      cooldown message).

### M14 — Obstacle avoidance (V2)
- [ ] With the robot exploring/idling, put an object ~15 cm ahead: it
      stops, looks left/right, backs off a little, turns away, returns to
      IDLE. Motors never push into the object.
- [ ] Close approach (< 12 cm) → SURPRISED (scared face, error sound,
      small hop back) then the avoid routine.
- [ ] Dashboard ▲ while an obstacle is inside the sonar cone → the
      forward command is rejected (STOP, `forward blocked` in serial).
- [ ] AI command `forward_small` with obstacle present → same rejection
      (AI cannot drive it into the obstacle).

### M15 — Fall / fault recovery (V2)
- [ ] Tilt the robot past ~65° for under a second → no fault (debounce).
- [ ] Lay it down: motors stop immediately, FAULT state, error LED,
      "Aduh!" on the OLED; dashboard IMU + LOGS agree.
- [ ] Any AI movement command during FAULT is ignored (movement lock).
- [ ] Set it upright again: after ~1.5 s → recovered, IDLE, happy beep.

---

## 3. Final acceptance checklist

### V1 core
- [ ] ESP32 boots
- [ ] OLED works
- [ ] motors work
- [ ] stop works
- [ ] touch works
- [ ] debounce works
- [ ] buzzer works
- [ ] WS2812 works
- [ ] local personality works
- [ ] sleep/wake works
- [ ] Wi-Fi connects
- [ ] WebSocket connects
- [ ] ESP32 sends events
- [ ] Local AI receives events
- [ ] Local AI sends commands
- [ ] ESP32 executes commands
- [ ] movement auto-stops
- [ ] invalid JSON is safe
- [ ] offline mode works
- [ ] reconnect works without reboot
- [ ] dashboard works
- [ ] dashboard controls movement
- [ ] dashboard tests emotions/animations
- [ ] dashboard logs events
- [ ] Touch -> AI -> robot response works
- [ ] AI failure fallback works
- [ ] existing Local AI features still work

### V2 perception
- [ ] ESP32-CAM unit boots and links to the robot
- [ ] camera stream visible (LAN) and proxied in dashboard
- [ ] basic person detection works (motion baseline, M12/M13)
- [ ] target direction (zone) works
- [ ] HC-SR04 works, filtered distance is stable
- [ ] obstacle detection works (hysteresis holds)
- [ ] obstacle avoidance works (stop → inspect → back off → turn)
- [ ] close-obstacle SURPRISED works
- [ ] IMU works (orientation, motion, health)
- [ ] fall latched → FAULT: motors dead, error output
- [ ] recovery: upright grace → IDLE + beep
- [ ] perception layer fuses sensors (no direct sensor→behavior wiring)
- [ ] world_state reaches dashboard + AI context
- [ ] CURIOUS works
- [ ] FOLLOW works (paced, bounded, cooldown)
- [ ] SEARCH works
- [ ] INTERACT works
- [ ] protocol updated safely (V1 messages still accepted)
- [ ] Local AI receives useful perception data
- [ ] dashboard exposes V2 telemetry (VISION/DISTANCE/IMU/WORLD)
- [ ] offline safety behavior works (vision loss / link loss = safe)
- [ ] failure cases handled (sonar/IMU/camera unplugged = no crash)
- [ ] sensor-absent boot works (robot functional without V2 hardware)

*Automated portions already verified in this repo: protocol parsing +
vision frames, invalid JSON safety, fallback pipeline, world_state
telemetry + AI context, dashboard command delivery, E2E WebSocket
round-trip, both firmware compilations. Hardware-dependent behaviors
(M3–M15) require bench verification before being claimed as working.*
