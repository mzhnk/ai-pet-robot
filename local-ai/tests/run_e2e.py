#!/usr/bin/env python3
"""
run_e2e.py — end-to-end integration test with a simulated ESP32.

Boots the real Flask app on a test port, connects over a REAL WebSocket
(websocket-client), and exercises the full pipeline:

  ESP32(sim) -> WS -> Local AI -> (AI mocked / fallback) -> WS -> ESP32(sim)
  Dashboard REST -> WS -> ESP32(sim)

Usage:  .venv/bin/python tests/run_e2e.py
Exit 0 = all checks passed.
"""
import json
import sys
import threading
import time

sys.path.insert(0, ".")

import requests                      # noqa: E402
import websocket                     # noqa: E402

import config                        # noqa: E402
from ai_router import ai_router      # noqa: E402

PORT = 8799
BASE = f"http://127.0.0.1:{PORT}"
WS_URL = f"ws://127.0.0.1:{PORT}/ws/robot"

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    mark = "PASS" if ok else "FAIL"
    print(f"[{mark}] {name}" + (f" — {detail}" if detail else ""))


def _wait_log(fragment, timeout_s=3.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            logs = requests.get(f"{BASE}/api/robot/logs", timeout=2).json()["logs"]
            if any(fragment in e["message"] for e in logs):
                return True
        except requests.RequestException:
            pass
        time.sleep(0.1)
    return False


def start_server():
    from app import app
    threading.Thread(
        target=lambda: app.run(host="127.0.0.1", port=PORT, threaded=True,
                               use_reloader=False),
        daemon=True,
    ).start()


def wait_http(timeout_s=10.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            if requests.get(f"{BASE}/api/health", timeout=1).ok:
                return True
        except requests.RequestException:
            time.sleep(0.2)
    return False


class FakeEsp32:
    """Minimal ESP32 stand-in over a real WebSocket connection."""

    def __init__(self):
        self.ws = websocket.create_connection(WS_URL, timeout=5)
        self.commands = []

    def send(self, payload: dict):
        self.ws.send(json.dumps(payload))

    def send_raw(self, raw: str):
        self.ws.send(raw)

    def recv_command(self, timeout_s=10.0):
        """Read frames until a robot command (has 'emotion') arrives."""
        deadline = time.time() + timeout_s
        self.ws.settimeout(0.5)
        while time.time() < deadline:
            try:
                frame = self.ws.recv()
            except websocket.WebSocketTimeoutException:
                continue
            try:
                data = json.loads(frame)
            except json.JSONDecodeError:
                continue
            self.commands.append(data)
            if isinstance(data, dict) and "emotion" in data:
                return data
        return None

    def close(self):
        try:
            self.ws.close()
        except Exception:
            pass


def main() -> int:
    start_server()
    check("server boots and /api/health answers", wait_http())

    # Mock the AI (no API key in CI): deterministic success path.
    ai_router.chat = lambda messages, timeout_s=None: (
        '{"emotion":"happy","animation":"smile","movement":"turn_left",'
        '"speech":"Halo! Senang kamu menyentuhku."}', "mock-provider")

    # ---- connect & heartbeat ----
    esp = FakeEsp32()
    check("fake ESP32 websocket connects", esp.ws.connected)

    esp.send({"event": "heartbeat", "uptime_s": 12, "rssi": -55,
              "wifi": True, "state": "IDLE", "emotion": "neutral"})
    time.sleep(0.4)
    status = requests.get(f"{BASE}/api/robot/status", timeout=3).json()["robot"]
    check("server sees robot connected", status["connected"] is True)
    check("telemetry mirrored to status (rssi/state/uptime)",
          status["rssi"] == -55 and status["behavior_state"] == "IDLE"
          and status["uptime_s"] == 12)

    # ---- boot_completed ----
    esp.send({"event": "boot_completed"})
    cmd = esp.recv_command(timeout_s=5)
    check("boot_completed triggers AI greeting command",
          cmd is not None and cmd["emotion"] == "happy", json.dumps(cmd))

    # ---- touch -> AI -> command (success path) ----
    esp.send({"event": "touch_head"})
    cmd = esp.recv_command(timeout_s=5)
    check("touch_head -> AI -> robot command",
          cmd is not None and cmd.get("movement") == "turn_left"
          and cmd.get("emotion") == "happy", json.dumps(cmd))

    # ---- touch -> fallback path (AI broken) ----
    def boom(messages, timeout_s=None):
        from ai_router import AIRouterError
        raise AIRouterError("all providers down (simulated)")

    ai_router.chat = boom
    esp.send({"event": "touch_head"})
    cmd = esp.recv_command(timeout_s=5)
    check("AI failure produces safe fallback command",
          cmd is not None and cmd.get("fallback") is True
          and cmd.get("movement") == "stop", json.dumps(cmd))

    # ---- malformed + unknown payloads must not kill anything ----
    esp.send_raw("this is { not json at all")
    esp.send_raw("[1,2,3]")
    esp.send({"event": "total_eclipse"})
    esp.send({"event": "heartbeat", "uptime_s": 13, "rssi": -56,
              "wifi": True, "state": "IDLE", "emotion": "neutral"})
    time.sleep(0.4)
    status = requests.get(f"{BASE}/api/robot/status", timeout=3).json()
    check("malformed/unknown payloads ignored safely",
          status["robot"]["connected"] is True and status["robot"]["uptime_s"] == 13)
    logs = requests.get(f"{BASE}/api/robot/logs", timeout=3).json()["logs"]
    types = {e["type"] for e in logs}
    check("errors were logged for bad payloads", "error" in types)

    # ---- dashboard REST -> WS -> robot ----
    resp = requests.post(f"{BASE}/api/robot/command",
                         json={"movement": "forward_small", "speed": 75,
                               "emotion": "curious"},
                         timeout=3).json()
    check("dashboard command accepted", resp.get("ok") is True, json.dumps(resp))
    delivered = esp.recv_command(timeout_s=5)
    check("dashboard command reaches ESP32 over WS",
          delivered is not None and delivered.get("movement") == "forward_small"
          and delivered.get("speed") in range(200, 256), json.dumps(delivered or {}))

    # ---- logs endpoint shows the full story ----
    logs = requests.get(f"{BASE}/api/robot/logs", timeout=3).json()["logs"]
    log_text = " ".join(e["message"] for e in logs)
    check("logs contain connection/event/command/error entries",
          all(t in types for t in ("connection", "event", "command", "error")))
    check("AI flow visible in logs", "mock-provider" in log_text
          or "AI unavailable" in log_text)

    # ============================================================
    # V2: perception pipeline
    # ============================================================

    # ---- world_state -> dashboard snapshot ----
    esp.send({"event": "world_state", "person": True, "zone": "center",
              "distance": 45.5, "obstacle": False, "close": False,
              "upright": True, "motion": "moving", "state": "FOLLOW",
              "vision": True})
    time.sleep(0.4)
    status = requests.get(f"{BASE}/api/robot/status", timeout=3).json()["robot"]
    world = status.get("world") or {}
    check("world_state mirrored to dashboard snapshot",
          world.get("person") is True and world.get("zone") == "center"
          and world.get("distance") == 45.5 and world.get("vision") is True,
          json.dumps(world))
    check("world_state drives behavior_state in status",
          status["behavior_state"] == "FOLLOW")

    # ---- person_detected (greeting cooldown applies) ----
    esp.send({"event": "person_detected", "zone": "right"})
    check("person_detected accepted and logged",
          _wait_log("person_detected"))

    # ---- obstacle_cleared accepted ----
    esp.send({"event": "obstacle_cleared"})
    check("obstacle_cleared accepted and logged",
          _wait_log("obstacle_cleared"))

    # ---- fall_detected: deterministic local handling (no AI roundtrip) ----
    ai_router.chat = boom   # ensure any AI call would fail loudly
    esp.send({"event": "fall_detected"})
    check("fall_detected accepted and logged",
          _wait_log("fall_detected"))
    status = requests.get(f"{BASE}/api/robot/status", timeout=3).json()["robot"]
    check("fall_detected does not break the link",
          status["connected"] is True)

    # ---- camera proxy degrades cleanly without a linked camera ----
    resp = requests.get(f"{BASE}/api/robot/camera/stream", timeout=5)
    check("camera proxy 503 with no camera linked", resp.status_code == 503,
          f"status={resp.status_code}")

    # ---- disconnect handling ----
    esp.close()
    time.sleep(1.5)
    status = requests.get(f"{BASE}/api/robot/status", timeout=3).json()["robot"]
    check("disconnect reflected in status", status["connected"] is False)

    # ---- summary ----
    failed = [name for name, ok, _ in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    if failed:
        print("FAILED:", *failed, sep="\n  - ")
        return 1
    print("E2E OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
