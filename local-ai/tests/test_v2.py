"""
test_v2.py — V2 perception-layer tests (no hardware, no network).

Covers:
  - build_world_context (AI prompt context rendering)
  - robot_state.mark_world_state (whitelist, typing, FSM transition log)
  - V2 simulated events: person / person_lost / fall / world_state
  - camera stream proxy degrades to 503 without a linked camera
"""
import time

import pytest

import routes.robot_ws as robot_ws
from app import create_app
from robot_behavior import build_world_context
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
    robot_ws._robot_ws = None
    robot_state._logs.clear()
    robot_state.connected = False
    robot_state.last_command = None
    robot_state.world = {}
    robot_state.camera_ip = None
    yield
    robot_ws._robot_ws = None


# ------------------------------------------------------------
# build_world_context (pure function)
# ------------------------------------------------------------

def test_world_context_empty_when_no_data():
    assert build_world_context(None) == ""
    assert build_world_context({}) == ""


def test_world_context_person_with_zone_and_distance():
    ctx = build_world_context({"person": True, "zone": "left", "distance": 42.4})
    assert "kiri" in ctx
    assert "42" in ctx
    assert "Konteks persepsi" in ctx


def test_world_context_no_person():
    ctx = build_world_context({"person": False})
    assert "Tidak ada orang" in ctx


def test_world_context_obstacle_warning():
    ctx = build_world_context({"person": False, "obstacle": True, "close": True})
    assert "HALANGAN" in ctx
    assert "diblokir" in ctx


def test_world_context_not_upright():
    ctx = build_world_context({"person": False, "upright": False})
    assert "tidak tegak" in ctx


def test_world_context_ignores_nonsense_types():
    ctx = build_world_context({"person": "yes-ish", "zone": 123, "distance": "far"})
    assert isinstance(ctx, str) and ctx


# ------------------------------------------------------------
# robot_state: world_state marking
# ------------------------------------------------------------

def test_mark_world_state_whitelists_fields():
    robot_state.mark_world_state({
        "event": "world_state",
        "person": True, "zone": "center", "distance": 55.5,
        "obstacle": False, "close": False, "upright": True,
        "motion": "moving", "state": "FOLLOW", "vision": True,
        "injected_extra": "must-not-appear",
    })
    world = robot_state.snapshot()["world"]
    assert world["person"] is True
    assert world["zone"] == "center"
    assert world["distance"] == 55.5
    assert world["vision"] is True
    assert "injected_extra" not in world


def test_mark_world_state_bad_distance_becomes_none():
    robot_state.mark_world_state({"event": "world_state", "distance": "near"})
    assert robot_state.snapshot()["world"]["distance"] is None


def test_mark_world_state_logs_fsm_transition():
    robot_state.mark_heartbeat({"event": "heartbeat", "state": "IDLE"})
    robot_state._logs.clear()   # isolate the transition we care about
    robot_state.mark_world_state({"event": "world_state", "state": "FOLLOW"})
    messages = [e["message"] for e in robot_state.get_logs()]
    assert any("IDLE -> FOLLOW" in m for m in messages)


def test_mark_world_state_sets_timestamp():
    before = time.time()
    robot_state.mark_world_state({"event": "world_state"})
    ts = robot_state.snapshot()["world_updated_ts"]
    assert ts is not None and ts >= before


def test_snapshot_contains_v2_keys():
    snap = robot_state.snapshot()
    for key in ("world", "world_updated_ts", "camera_ip"):
        assert key in snap


# ------------------------------------------------------------
# V2 simulated events (dashboard testers)
# ------------------------------------------------------------

def _wait_for(predicate, timeout_s=3.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return False


def test_event_person_detected(client):
    resp = client.post("/api/robot/event", json={"type": "person", "zone": "right"})
    assert resp.status_code == 200
    assert _wait_for(lambda: any(
        "person_detected" in e["message"]
        for e in robot_state.get_logs()))


def test_event_person_lost(client):
    resp = client.post("/api/robot/event", json={"type": "person_lost"})
    assert resp.status_code == 200
    assert _wait_for(lambda: any(
        "person_lost" in e["message"]
        for e in robot_state.get_logs()))


def test_event_fall_is_logged_without_ai(client):
    """Fall is handled locally on the ESP32; the server must NOT call the AI."""
    def boom(messages, timeout_s=None):
        raise AssertionError("AI must not be called for fall_detected")

    from ai_router import ai_router
    monkeypatched = False
    try:
        orig = ai_router.chat
        ai_router.chat = boom
        monkeypatched = True
        resp = client.post("/api/robot/event", json={"type": "fall"})
        assert resp.status_code == 200
        time.sleep(0.3)   # give a wrongly-spawned thread time to fail
        assert _wait_for(lambda: any(
            "fall_detected" in e["message"]
            for e in robot_state.get_logs()))
    finally:
        if monkeypatched:
            ai_router.chat = orig


def test_event_world_state_updates_snapshot(client):
    resp = client.post("/api/robot/event", json={
        "type": "world_state", "person": True, "zone": "center",
        "distance": 33.3, "obstacle": True, "vision": True,
    })
    assert resp.status_code == 200
    assert _wait_for(lambda: robot_state.snapshot()["world"].get("person") is True)
    world = robot_state.snapshot()["world"]
    assert world["zone"] == "center"
    assert world["distance"] == 33.3
    assert world["obstacle"] is True


def test_event_invalid_type_still_rejected(client):
    assert client.post("/api/robot/event", json={"type": "volcano"}).status_code == 400


# ------------------------------------------------------------
# Camera stream proxy
# ------------------------------------------------------------

def test_camera_stream_503_without_camera(client):
    resp = client.get("/api/robot/camera/stream")
    assert resp.status_code == 503
    body = resp.get_json()
    assert body["ok"] is False
    assert "camera" in body["error"].lower()


def test_camera_stream_503_when_camera_unreachable(client):
    robot_state.set_camera_ip("203.0.113.99")   # TEST-NET-3: never routable
    resp = client.get("/api/robot/camera/stream")
    assert resp.status_code == 503
