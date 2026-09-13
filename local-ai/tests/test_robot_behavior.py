"""
test_robot_behavior.py — unit tests for the AI -> command translation layer.

Covers: vocabulary validation, safe degradation, speech clipping,
speed/duration clamping, AI text parsing (JSON / wrapped JSON / prose),
and the fallback command.
"""
import pytest

import config
from robot_behavior import (
    ALLOWED_ANIMATIONS,
    ALLOWED_EMOTIONS,
    ALLOWED_MOVEMENTS,
    build_command_from_ai_text,
    build_manual_command,
    fallback_command,
    validate_command,
)


# ------------------------------------------------------------
# validate_command
# ------------------------------------------------------------

def test_validate_command_keeps_valid_fields():
    cmd = validate_command({
        "emotion": "happy",
        "animation": "smile",
        "movement": "forward_small",
        "speech": "Halo!",
    })
    assert cmd["emotion"] == "happy"
    assert cmd["animation"] == "smile"
    assert cmd["movement"] == "forward_small"
    assert cmd["speech"] == "Halo!"


def test_validate_command_rejects_bad_emotion():
    with pytest.raises(ValueError):
        validate_command({"emotion": "ecstatic"})


def test_validate_command_rejects_non_dict():
    with pytest.raises(ValueError):
        validate_command(["happy"])          # type: ignore[arg-type]
    with pytest.raises(ValueError):
        validate_command("happy")            # type: ignore[arg-type]


def test_validate_command_drops_unknown_animation_and_movement():
    cmd = validate_command({"emotion": "neutral", "animation": "dance",
                            "movement": "fly"})
    assert "animation" not in cmd
    assert "movement" not in cmd


def test_validate_command_clamps_speech_length():
    cmd = validate_command({"emotion": "neutral", "speech": "x" * 500})
    assert len(cmd["speech"]) == config.ROBOT_SPEECH_MAX_CHARS


def test_validate_command_clamps_speed_and_duration():
    cmd = validate_command({"emotion": "neutral", "movement": "forward_small",
                            "speed": 999, "duration_ms": 99999})
    assert cmd["speed"] == 255
    assert cmd["duration_ms"] == 1500

    cmd2 = validate_command({"emotion": "neutral", "movement": "stop",
                             "speed": -5, "duration_ms": 0})
    assert "speed" not in cmd2
    assert "duration_ms" not in cmd2


def test_internal_emotion_thinking_is_allowed_for_dashboard():
    cmd = validate_command({"emotion": "thinking"})
    assert cmd["emotion"] == "thinking"


# ------------------------------------------------------------
# build_command_from_ai_text
# ------------------------------------------------------------

def test_ai_text_pure_json():
    text = '{"emotion":"happy","animation":"smile","movement":"turn_left","speech":"Yeay!"}'
    cmd = build_command_from_ai_text(text)
    assert cmd["emotion"] == "happy"
    assert cmd["animation"] == "smile"
    assert cmd["movement"] == "turn_left"


def test_ai_text_json_in_markdown_fence():
    text = "```json\n{\"emotion\":\"curious\",\"speech\":\"Hmm?\"}\n```"
    cmd = build_command_from_ai_text(text)
    assert cmd["emotion"] == "curious"
    assert cmd["animation"] == "blink"       # default filled in
    assert cmd["movement"] == "stop"


def test_ai_text_prose_becomes_neutral_speech():
    cmd = build_command_from_ai_text("Halo! Senang bertemu denganmu.")
    assert cmd["emotion"] == "neutral"
    assert cmd["speech"] == "Halo! Senang bertemu denganmu."
    assert cmd["movement"] == "stop"


def test_ai_text_empty_is_safe():
    cmd = build_command_from_ai_text("")
    assert cmd["emotion"] == "neutral"


def test_ai_text_broken_json_is_wrapped_not_fatal():
    cmd = build_command_from_ai_text('{"emotion": happy, broken')
    assert cmd["emotion"] == "neutral"
    assert "emotion" in cmd["speech"]


# ------------------------------------------------------------
# fallback
# ------------------------------------------------------------

def test_fallback_command_is_valid_and_marked():
    cmd = fallback_command("test reason")
    assert cmd["fallback"] is True
    assert cmd["emotion"] in ALLOWED_EMOTIONS
    assert cmd["movement"] == "stop"
    assert cmd["speech"]


def test_fallback_reason_is_short():
    cmd = fallback_command("x" * 500)
    assert len(cmd["fallback_reason"]) <= 120


# ------------------------------------------------------------
# manual builder (dashboard)
# ------------------------------------------------------------

def test_manual_command_defaults_to_neutral():
    cmd = build_manual_command()
    assert cmd["emotion"] == "neutral"
    assert cmd["movement"] == "stop"


def test_manual_command_with_all_fields():
    cmd = build_manual_command(emotion="happy", animation="tilt",
                               movement="turn_right", speed=80, duration_ms=400)
    assert cmd["emotion"] == "happy"
    assert cmd["animation"] == "tilt"
    assert cmd["movement"] == "turn_right"
    assert cmd["speed"] == 80
    assert cmd["duration_ms"] == 400


def test_allowed_vocabularies_match_firmware_contract():
    # Must mirror esp32-firmware/src/protocol/protocol.h tables.
    assert set(ALLOWED_EMOTIONS) == {"neutral", "happy", "curious", "sleepy", "scared"}
    assert set(ALLOWED_ANIMATIONS) == {"smile", "blink", "look_left", "look_right", "tilt"}
    assert set(ALLOWED_MOVEMENTS) == {
        "forward_small", "backward_small", "turn_left", "turn_right", "stop"}
