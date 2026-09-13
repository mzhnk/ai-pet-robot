"""
routes/robot_ws.py — robot integration endpoint.

Owns:
  - /ws/robot          WebSocket for the ESP32 (flask-sock / simple-websocket)
  - /api/robot/*       REST API consumed by the developer dashboard
  - Touch -> AI -> command flow with a safe fallback

Isolation rule: nothing here touches other AI features; the only shared
pieces are `ai_router` and `robot_state`.
"""
import json
import logging
import threading
import time

import requests
from flask import Blueprint, Response, jsonify, request

import config
from ai_router import AIRouterError, ai_router
from extensions import sock
from robot_behavior import (
    ROBOT_SYSTEM_PROMPT,
    build_command_from_ai_text,
    build_manual_command,
    build_world_context,
    fallback_command,
)
from robot_state import robot_state

logger = logging.getLogger(__name__)

robot_bp = Blueprint("robot", __name__)

# ------------------------------------------------------------
# WebSocket registry (single robot)
# ------------------------------------------------------------

_ws_lock = threading.Lock()
_robot_ws = None          # the live robot websocket (or None)
_ai_in_flight = False     # prevents stacking parallel AI replies
_last_person_greet_ts = 0.0   # V2: cooldown between AI greetings


def send_to_robot(command: dict) -> bool:
    """Push a command JSON to the ESP32. Safe to call from any thread."""
    global _robot_ws
    with _ws_lock:
        ws = _robot_ws
    if ws is None:
        robot_state.log("error", "cannot send command: robot not connected")
        return False
    try:
        ws.send(json.dumps(command, ensure_ascii=False))
        return True
    except Exception as exc:                      # socket died mid-send
        robot_state.log("error", f"send failed: {type(exc).__name__}: {exc}")
        return False


def _register_ws(ws) -> None:
    global _robot_ws
    with _ws_lock:
        if _robot_ws is not None:
            robot_state.log("connection", "second robot socket; replacing old one")
        _robot_ws = ws
    robot_state.mark_connected()


def _unregister_ws(ws) -> None:
    global _robot_ws
    with _ws_lock:
        if _robot_ws is ws:
            _robot_ws = None
    robot_state.mark_disconnected("")


# ------------------------------------------------------------
# Touch -> AI -> command flow
# ------------------------------------------------------------

def _dispatch_ai_reaction(user_prompt: str) -> None:
    """
    Runs in a worker thread: ask the AI for a reaction and push it to the
    robot. Every failure path ends in a safe fallback command — the pet
    never goes silent just because an API is down.
    """
    global _ai_in_flight
    try:
        messages = [
            {"role": "system", "content": ROBOT_SYSTEM_PROMPT},
            {"role": "user", "content": user_prompt},
        ]
        try:
            reply, provider = ai_router.chat(messages)
        except AIRouterError as exc:
            robot_state.log("error", f"AI unavailable: {exc}")
            command = fallback_command(str(exc))
        else:
            robot_state.log("event", f"AI reply via {provider}")
            command = build_command_from_ai_text(reply)

        robot_state.record_command(command)
        send_to_robot(command)
    except Exception as exc:                       # last-resort guard
        robot_state.log("error", f"AI flow crashed: {type(exc).__name__}: {exc}")
    finally:
        _ai_in_flight = False


def _start_ai_reaction(user_prompt: str) -> None:
    global _ai_in_flight
    with _ws_lock:
        busy = _ai_in_flight
        _ai_in_flight = True
    if busy:
        robot_state.log("event", "AI reaction skipped (one already in flight)")
        return
    threading.Thread(
        target=_dispatch_ai_reaction,
        args=(user_prompt,),
        daemon=True,
        name="robot-ai-reaction",
    ).start()


def _world_context_suffix() -> str:
    """V2: current perception snapshot as prompt context (may be empty)."""
    world = robot_state.snapshot().get("world") or {}
    return build_world_context(world)


# ------------------------------------------------------------
# ESP32 -> Local AI message handling
# ------------------------------------------------------------

def _handle_robot_message(payload: str) -> None:
    global _last_person_greet_ts
    try:
        data = json.loads(payload)
        if not isinstance(data, dict):
            raise ValueError("not an object")
    except (json.JSONDecodeError, ValueError) as exc:
        robot_state.log("error", f"malformed robot payload ({exc}); ignored")
        return

    event = data.get("event")

    if event == "heartbeat":
        robot_state.mark_heartbeat(data)

    elif event == "touch_head":
        robot_state.log("event", "touch_head received from robot")
        robot_state.touch_last_seen()
        prompt = ("Pengguna baru saja menyentuh kepalamu. "
                  "Balas dengan reaksi singkat yang menggembirakan.")
        context = _world_context_suffix()
        if context:
            prompt = f"{context}\n{prompt}"
        _start_ai_reaction(prompt)

    elif event == "boot_completed":
        robot_state.log("event", "boot_completed: robot just finished booting")
        robot_state.touch_last_seen()
        _start_ai_reaction("Robot baru saja menyala dan menyapa. Kirim sapaan pembuka singkat.")

    elif event == "obstacle_detected":
        robot_state.log("event", "obstacle_detected")
        robot_state.touch_last_seen()
        command = build_manual_command(
            emotion="scared", movement="stop", speech="Wah, ada halangan!",
        )
        robot_state.record_command(command)
        send_to_robot(command)

    elif event == "obstacle_cleared":
        robot_state.log("event", "obstacle_cleared")
        robot_state.touch_last_seen()

    elif event == "person_detected":
        robot_state.touch_last_seen()
        zone = data.get("zone", "none")
        now = time.time()
        if now - _last_person_greet_ts >= config.ROBOT_PERSON_GREET_COOLDOWN_S:
            _last_person_greet_ts = now
            robot_state.log("event", f"person_detected (zone={zone}) -> AI greeting")
            prompt = (
                f"Seseorang baru saja terdeteksi oleh kameramu di arah {zone}. "
                "Sambut dengan penuh rasa penasaran dan senang."
            )
            context = _world_context_suffix()
            if context:
                prompt = f"{context}\n{prompt}"
            _start_ai_reaction(prompt)
        else:
            robot_state.log("event", f"person_detected (zone={zone}); greeting on cooldown")

    elif event == "person_lost":
        robot_state.log("event", "person_lost")
        robot_state.touch_last_seen()

    elif event == "fall_detected":
        robot_state.log("event", "fall_detected (robot handles recovery locally)")
        robot_state.touch_last_seen()
    elif event == "world_state":
        robot_state.mark_world_state(data)

    else:
        robot_state.log("error", f"unknown robot event {event!r}; ignored")


@sock.route("/ws/robot")
def robot_websocket(ws):
    """ESP32 entry point. One robot at a time; latest socket wins."""
    _register_ws(ws)
    try:
        while True:
            message = ws.receive()
            if message is None:
                break
            robot_state.touch_last_seen()
            _handle_robot_message(message)
    except Exception as exc:
        robot_state.log("error", f"websocket error: {type(exc).__name__}: {exc}")
    finally:
        _unregister_ws(ws)


def _heartbeat_monitor() -> None:
    """Detect silent robot deaths (no TCP FIN) and reflect them in state."""
    while True:
        time.sleep(2.0)
        try:
            with _ws_lock:
                ws = _robot_ws
            if ws is not None and not robot_state.is_alive():
                robot_state.mark_disconnected("(heartbeat timeout)")
        except Exception as exc:                   # monitor must never die
            logger.debug("monitor error: %s", exc)


# ------------------------------------------------------------
# REST API for the developer dashboard
# ------------------------------------------------------------

def _speed_pwm_from_percent(percent: float) -> int:
    span = config.ROBOT_SPEED_MAX_PWM - config.ROBOT_SPEED_MIN_PWM
    pct = max(0, min(100, float(percent)))
    return int(config.ROBOT_SPEED_MIN_PWM + round(span * pct / 100.0))


@robot_bp.get("/api/robot/status")
def api_robot_status():
    return jsonify({
        "robot": robot_state.snapshot(),
        "brain": ai_router.brain_status(),
        "server": {"ts": time.time()},
    })


@robot_bp.post("/api/robot/command")
def api_robot_command():
    body = request.get_json(silent=True) or {}
    try:
        speed = body.get("speed")
        command = build_manual_command(
            emotion=body.get("emotion"),
            animation=body.get("animation"),
            movement=body.get("movement"),
            speech=body.get("speech"),
            speed=_speed_pwm_from_percent(speed) if speed is not None else None,
            duration_ms=body.get("duration_ms"),
        )
    except ValueError as exc:
        return jsonify({"ok": False, "error": str(exc)}), 400

    robot_state.record_command(command)
    sent = send_to_robot(command)
    return jsonify({"ok": sent, "command": command})


@robot_bp.post("/api/robot/event")
def api_robot_simulate_event():
    """Dashboard event tester: inject an event as if the ESP32 sent it."""
    body = request.get_json(silent=True) or {}
    event = body.get("type", "")

    if event == "touch":
        _handle_robot_message(json.dumps({"event": "touch_head"}))
        return jsonify({"ok": True, "injected": event})

    if event == "obstacle":
        _handle_robot_message(json.dumps({"event": "obstacle_detected"}))
        return jsonify({"ok": True, "injected": event})

    if event == "obstacle_clear":
        _handle_robot_message(json.dumps({"event": "obstacle_cleared"}))
        return jsonify({"ok": True, "injected": event})

    if event == "person":
        zone = body.get("zone", "center")
        _handle_robot_message(json.dumps(
            {"event": "person_detected", "zone": zone}))
        return jsonify({"ok": True, "injected": event})

    if event == "person_lost":
        _handle_robot_message(json.dumps({"event": "person_lost"}))
        return jsonify({"ok": True, "injected": event})

    if event == "fall":
        _handle_robot_message(json.dumps({"event": "fall_detected"}))
        return jsonify({"ok": True, "injected": event})

    if event == "boot":
        _handle_robot_message(json.dumps({"event": "boot_completed"}))
        return jsonify({"ok": True, "injected": event})

    if event == "world_state":
        sample = {
            "event": "world_state",
            "person": body.get("person", False),
            "zone": body.get("zone", "none"),
            "distance": body.get("distance"),
            "obstacle": body.get("obstacle", False),
            "close": body.get("close", False),
            "upright": body.get("upright", True),
            "motion": body.get("motion", "still"),
            "state": body.get("state", "IDLE"),
            "vision": body.get("vision", False),
        }
        _handle_robot_message(json.dumps(sample))
        return jsonify({"ok": True, "injected": event})

    return jsonify({
        "ok": False,
        "error": ("type must be touch|obstacle|obstacle_clear|person|"
                  "person_lost|fall|boot|world_state"),
    }), 400


@robot_bp.get("/api/robot/logs")
def api_robot_logs():
    since = float(request.args.get("since", 0) or 0)
    limit = min(int(request.args.get("limit", 200) or 200), 500)
    return jsonify({"logs": robot_state.get_logs(since_ts=since, limit=limit)})


# ------------------------------------------------------------
# V2: camera MJPEG proxy
# Browser -> Local AI -> camera unit. The browser never talks to the
# robot or camera directly, and a dead camera degrades to a clean 503.
# ------------------------------------------------------------

@robot_bp.get("/api/robot/camera/stream")
def api_robot_camera_stream():
    camera_ip = robot_state.snapshot().get("camera_ip")
    if not camera_ip:
        return jsonify({"ok": False, "error": "camera unit not linked"}), 503

    url = f"http://{camera_ip}:{config.CAMERA_STREAM_PORT}/stream"

    try:
        upstream = requests.get(
            url,
            stream=True,
            timeout=config.CAMERA_STREAM_TIMEOUT_S,
        )
        upstream.raise_for_status()
    except requests.RequestException as exc:
        robot_state.log("error", f"camera stream unreachable: {exc}")
        return jsonify({"ok": False, "error": "camera stream unreachable"}), 503

    def relay():
        try:
            for chunk in upstream.iter_content(
                    chunk_size=config.CAMERA_STREAM_CHUNK):
                if chunk:
                    yield chunk
        finally:
            upstream.close()

    return Response(
        relay(),
        content_type=upstream.headers.get("Content-Type",
                                          "multipart/x-mixed-replace;boundary=frame"),
    )
