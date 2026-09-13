"""
robot_state.py — thread-safe runtime state + event log for the robot.

Single source of truth shared by:
  - routes/robot_ws.py  (websocket: ESP32 events in, commands out)
  - the developer dashboard REST API (status polling)

Everything is guarded by an RLock because Flask threads, the websocket
thread and the AI worker thread all touch this store.
"""
import threading
import time
from collections import deque
from typing import Optional

import config

LOG_TYPES = ("event", "command", "state", "error", "connection")


class RobotState:
    def __init__(self, log_max_entries: int = config.ROBOT_LOG_MAX_ENTRIES):
        self._lock = threading.RLock()
        self._logs = deque(maxlen=log_max_entries)

        self.connected = False
        self.last_seen: Optional[float] = None
        self.session_started: Optional[float] = None

        # Telemetry from the heartbeat payload
        self.uptime_s = 0
        self.rssi: Optional[int] = None
        self.wifi_connected = False
        self.behavior_state = "UNKNOWN"
        self.emotion = "neutral"

        # V2: perception / world state (updated by world_state events)
        self.world: dict = {}
        self.camera_ip: Optional[str] = None
        self.world_updated_ts: Optional[float] = None

        self.last_command: Optional[dict] = None
        self.last_command_ts: Optional[float] = None

    # ------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------

    def mark_connected(self) -> None:
        with self._lock:
            self.connected = True
            self.last_seen = time.time()
            self.session_started = time.time()
            self.log("connection", "robot websocket CONNECTED")

    def mark_disconnected(self, reason: str = "") -> None:
        with self._lock:
            self.connected = False
            self.log("connection", f"robot websocket DISCONNECTED {reason}".strip())

    def mark_heartbeat(self, payload: dict) -> None:
        """Update telemetry from an ESP32 heartbeat/status event."""
        with self._lock:
            self.last_seen = time.time()
            self.uptime_s = int(payload.get("uptime_s", self.uptime_s) or 0)
            rssi = payload.get("rssi")
            self.rssi = int(rssi) if isinstance(rssi, (int, float)) else self.rssi
            self.wifi_connected = bool(payload.get("wifi", self.wifi_connected))
            state = payload.get("state")
            if isinstance(state, str) and state:
                if state != self.behavior_state:
                    self.log("state", f"FSM {self.behavior_state} -> {state}")
                self.behavior_state = state
            emotion = payload.get("emotion")
            if isinstance(emotion, str) and emotion:
                self.emotion = emotion

    def mark_world_state(self, payload: dict) -> None:
        """V2: update the perception snapshot from a world_state event."""
        with self._lock:
            self.last_seen = time.time()
            self.world_updated_ts = time.time()
            # Whitelist + type-check: never trust the wire blindly.
            self.world = {
                "person": bool(payload.get("person", False)),
                "zone": str(payload.get("zone", "none"))[:12],
                "distance": payload.get("distance")
                if isinstance(payload.get("distance"), (int, float)) else None,
                "obstacle": bool(payload.get("obstacle", False)),
                "close": bool(payload.get("close", False)),
                "upright": bool(payload.get("upright", True)),
                "motion": str(payload.get("motion", "unknown"))[:12],
                "vision": bool(payload.get("vision", False)),
            }
            state = payload.get("state")
            if isinstance(state, str) and state and state != self.behavior_state:
                self.log("state", f"FSM {self.behavior_state} -> {state}")
                self.behavior_state = state

    def set_camera_ip(self, ip: Optional[str]) -> None:
        with self._lock:
            self.camera_ip = ip

    def touch_last_seen(self) -> None:
        with self._lock:
            self.last_seen = time.time()

    def is_alive(self, timeout_s: float = config.ROBOT_HEARTBEAT_TIMEOUT_S) -> bool:
        with self._lock:
            return self.connected and self.last_seen is not None and \
                (time.time() - self.last_seen) <= timeout_s

    # ------------------------------------------------------------
    # Commands
    # ------------------------------------------------------------

    def record_command(self, command: dict) -> None:
        with self._lock:
            self.last_command = command
            self.last_command_ts = time.time()
            self.log("command", f"-> robot {command}")

    # ------------------------------------------------------------
    # Logging
    # ------------------------------------------------------------

    def log(self, log_type: str, message: str) -> dict:
        """Append an entry; returns it. Unknown types become 'event'."""
        if log_type not in LOG_TYPES:
            log_type = "event"
        entry = {"ts": time.time(), "type": log_type, "message": str(message)[:300]}
        with self._lock:
            self._logs.append(entry)
        return entry

    def get_logs(self, since_ts: float = 0.0, limit: int = 200) -> list:
        with self._lock:
            entries = [e for e in self._logs if e["ts"] > since_ts]
        return entries[-limit:]

    # ------------------------------------------------------------
    # Snapshot (dashboard polling)
    # ------------------------------------------------------------

    def snapshot(self) -> dict:
        with self._lock:
            session_s = (time.time() - self.session_started) if self.session_started else 0
            return {
                "connected": self.connected,
                "alive": self.is_alive(),
                "last_seen": self.last_seen,
                "wifi": self.wifi_connected,
                "rssi": self.rssi,
                "uptime_s": self.uptime_s,
                "session_s": round(session_s, 1),
                "behavior_state": self.behavior_state,
                "emotion": self.emotion,
                "last_command": self.last_command,
                "last_command_ts": self.last_command_ts,
                # V2: perception snapshot ({} when no world_state yet)
                "world": dict(self.world),
                "world_updated_ts": self.world_updated_ts,
                "camera_ip": self.camera_ip,
            }


# Shared singleton.
robot_state = RobotState()
