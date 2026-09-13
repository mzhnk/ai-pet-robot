"""
test_routes.py — Flask endpoint tests (no hardware, no network).

Uses Flask's test client. AI calls are mocked so tests are deterministic.
"""
import time

import pytest

import routes.robot_ws as robot_ws
from ai_router import ai_router
from app import create_app
from robot_state import robot_state


@pytest.fixture()
def app():
    app = create_app()
    app.config["TESTING"] = True
    yield app


@pytest.fixture()
def client(app):
    return app.test_client()


@pytest.fixture(autouse=True)
def clean_state():
    """Fresh logs/state per test; also clear the fake ws registry."""
    robot_ws._robot_ws = None
    robot_state._logs.clear()
    robot_state.connected = False
    robot_state.last_command = None
    yield
    robot_ws._robot_ws = None


class FakeWs:
    """Collects everything the server 'sends to the robot'."""
    def __init__(self):
        self.sent = []

    def send(self, payload):
        self.sent.append(payload)


# ------------------------------------------------------------
# Basic endpoints
# ------------------------------------------------------------

def test_health(client):
    resp = client.get("/api/health")
    assert resp.status_code == 200
    assert resp.get_json()["ok"] is True


def test_dashboard_page_served(client):
    resp = client.get("/dashboard")
    assert resp.status_code == 200
    assert b"Developer Dashboard" in resp.data or b"dashboard" in resp.data.lower()


def test_index_page_served(client):
    assert client.get("/").status_code == 200


def test_robot_status_shape(client):
    data = client.get("/api/robot/status").get_json()
    assert "robot" in data and "brain" in data and "server" in data
    robot = data["robot"]
    for key in ("connected", "alive", "wifi", "rssi", "uptime_s",
                "behavior_state", "emotion"):
        assert key in robot


# ------------------------------------------------------------
# Command endpoint
# ------------------------------------------------------------

def test_command_without_robot_returns_ok_false(client):
    resp = client.post("/api/robot/command", json={"movement": "forward_small"})
    body = resp.get_json()
    assert resp.status_code == 200
    assert body["ok"] is False                      # nobody to send to
    assert body["command"]["movement"] == "forward_small"


def test_command_delivered_to_connected_robot(client):
    fake = FakeWs()
    robot_ws._robot_ws = fake
    resp = client.post("/api/robot/command",
                       json={"emotion": "happy", "animation": "smile"})
    body = resp.get_json()
    assert body["ok"] is True
    assert len(fake.sent) == 1
    assert '"emotion": "happy"' in fake.sent[0] or '"emotion":"happy"' in fake.sent[0]


def test_command_invalid_emotion_returns_400(client):
    resp = client.post("/api/robot/command", json={"emotion": "angry"})
    assert resp.status_code == 400


def test_command_speed_percent_maps_to_pwm(client):
    fake = FakeWs()
    robot_ws._robot_ws = fake
    client.post("/api/robot/command",
                json={"movement": "turn_left", "speed": 0})
    assert '"speed": 60' in fake.sent[0] or '"speed":60' in fake.sent[0]
    fake.sent.clear()
    client.post("/api/robot/command",
                json={"movement": "turn_left", "speed": 100})
    assert '"speed": 255' in fake.sent[0] or '"speed":255' in fake.sent[0]


# ------------------------------------------------------------
# Event simulation endpoint
# ------------------------------------------------------------

def _wait_for_log(fragment, timeout_s=3.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        for entry in robot_state.get_logs():
            if fragment in entry["message"]:
                return True
        time.sleep(0.05)
    return False


def test_event_requires_valid_type(client):
    resp = client.post("/api/robot/event", json={"type": "earthquake"})
    assert resp.status_code == 400


def test_event_touch_runs_ai_success_path(client, monkeypatch):
    fake = FakeWs()
    robot_ws._robot_ws = fake
    monkeypatch.setattr(
        ai_router, "chat",
        lambda messages, timeout_s=None: (
            '{"emotion":"happy","animation":"smile","movement":"turn_left",'
            '"speech":"Halo! Senang kamu menyentuhku."}', "mock-provider"),
    )
    resp = client.post("/api/robot/event", json={"type": "touch"})
    assert resp.status_code == 200

    assert _wait_for_log("AI reply via mock-provider")
    assert _wait_for_log("touch_head received")
    # The mocked AI command must reach the fake robot socket.
    deadline = time.time() + 3.0
    while time.time() < deadline and not fake.sent:
        time.sleep(0.05)
    assert fake.sent, "robot never received the AI command"
    assert "happy" in fake.sent[0]
    assert robot_state.last_command["movement"] == "turn_left"


def test_event_touch_falls_back_when_ai_fails(client, monkeypatch):
    fake = FakeWs()
    robot_ws._robot_ws = fake

    from ai_router import AIRouterError

    def boom(messages, timeout_s=None):
        raise AIRouterError("all providers down")

    monkeypatch.setattr(ai_router, "chat", boom)
    client.post("/api/robot/event", json={"type": "touch"})

    deadline = time.time() + 3.0
    while time.time() < deadline and not fake.sent:
        time.sleep(0.05)
    assert fake.sent, "fallback command never reached the robot"
    assert robot_state.last_command.get("fallback") is True
    assert _wait_for_log("AI unavailable")


def test_event_obstacle_sends_scared_stop(client):
    fake = FakeWs()
    robot_ws._robot_ws = fake
    client.post("/api/robot/event", json={"type": "obstacle"})
    deadline = time.time() + 2.0
    while time.time() < deadline and not fake.sent:
        time.sleep(0.05)
    assert '"scared"' in fake.sent[0]
    assert '"stop"' in fake.sent[0]


# ------------------------------------------------------------
# Logs endpoint
# ------------------------------------------------------------

def test_logs_endpoint_filters_by_since(client):
    robot_state.log("event", "old entry")
    time.sleep(0.01)
    cut = time.time()
    time.sleep(0.01)
    robot_state.log("event", "new entry")

    data = client.get("/api/robot/logs").get_json()
    messages = [e["message"] for e in data["logs"]]
    assert "old entry" in messages and "new entry" in messages

    data2 = client.get(f"/api/robot/logs?since={cut}").get_json()
    messages2 = [e["message"] for e in data2["logs"]]
    assert "new entry" in messages2 and "old entry" not in messages2


# ------------------------------------------------------------
# Chat endpoint (mocked AI)
# ------------------------------------------------------------

def test_chat_success(client, monkeypatch):
    monkeypatch.setattr(
        ai_router, "chat",
        lambda messages, timeout_s=None: ("Halo!", "mock-provider"),
    )
    data = client.post("/api/chat", json={"message": "hai"}).get_json()
    assert data["ok"] is True
    assert data["reply"] == "Halo!"
    assert data["provider"] == "mock-provider"


def test_chat_requires_message(client):
    assert client.post("/api/chat", json={"message": "  "}).status_code == 400


def test_chat_failure_returns_502(client, monkeypatch):
    from ai_router import AIRouterError

    def boom(messages, timeout_s=None):
        raise AIRouterError("down")

    monkeypatch.setattr(ai_router, "chat", boom)
    assert client.post("/api/chat", json={"message": "hi"}).status_code == 502
