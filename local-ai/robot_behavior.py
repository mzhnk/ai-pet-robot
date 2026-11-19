"""Translation layer between AI output and robot commands.

Responsibilities:
  - Define the allowed wire vocabulary (mirrors
    esp32-firmware/src/protocol/protocol.h tables).
  - Turn raw AI text into a validated robot command JSON.
  - Provide a safe fallback command when the AI is unreachable.
  - Validate manually built commands coming from the dashboard.

Pure logic with no I/O, so it is straightforward to unit-test.
"""
import json
import re
import time
from typing import Any, Optional

import config

# Wire vocabulary (must match the firmware protocol tables).

ALLOWED_EMOTIONS = ("neutral", "happy", "curious", "sleepy", "scared")
ALLOWED_INTERNAL_EMOTIONS = ALLOWED_EMOTIONS + ("thinking",)  # dashboard-only extra
ALLOWED_ANIMATIONS = ("smile", "blink", "look_left", "look_right", "tilt")
ALLOWED_MOVEMENTS = ("forward_small", "backward_small", "turn_left", "turn_right", "stop")

SPEED_MIN, SPEED_MAX = 1, 255
DURATION_MIN_MS, DURATION_MAX_MS = 50, 1500

# System prompt sent to the LLM alongside world-state context.
ROBOT_SYSTEM_PROMPT = """Kamu adalah PetRobot V2, robot peliharaan desktop yang kecil, \
ramah, dan ekspresif. Kamu punya sensor persepsi (kamera, jarak, IMU) sehingga \
kamu bisa tahu apa yang terjadi di sekitarmu. Kamu berbicara Bahasa Indonesia \
yang sederhana dan singkat.

Setiap kali pengguna menyentuh kepalamu atau bertanya, kamu WAJIB menjawab HANYA dengan \
satu objek JSON valid (tanpa penjelasan lain, tanpa markdown):

{"emotion": "...", "animation": "...", "movement": "...", "speech": "..."}

Aturan nilai (jangan pakai nilai lain):
- emotion: neutral | happy | curious | sleepy | scared
- animation: smile | blink | look_left | look_right | tilt
- movement: forward_small | backward_small | turn_left | turn_right | stop
- speech: maksimal 120 karakter, santai, hangat, khas robot peliharaan.

Pilih gerakan yang aman untuk robot meja; gunakan "stop" jika ragu."""


def build_world_context(world: Optional[dict]) -> str:
    """Render the perception snapshot as a short Indonesian context block.

    Pure function; returns "" when nothing is known yet, keeping prompts
    identical to the no-perception case.
    """
    if not isinstance(world, dict) or not world:
        return ""

    facts = []

    if world.get("person"):
        zone_id = {"left": "kiri", "center": "tengah", "right": "kanan"}.get(
            world.get("zone", "none"))
        facts.append(f"Ada orang terdeteksi di arah {zone_id or 'tidak diketahui'}")
        dist = world.get("distance")
        if isinstance(dist, (int, float)):
            near = "dekat" if dist <= 60 else "agak jauh"
            facts.append(f"jarak sekitar {int(dist)} cm ({near})")
    else:
        facts.append("Tidak ada orang terdeteksi sekarang")

    if world.get("obstacle"):
        facts.append("ADA HALANGAN di dekat robot (gerakan maju akan diblokir otomatis)")
    if not world.get("upright", True):
        facts.append("Robot tidak tegak!")
    if world.get("motion") == "moving":
        facts.append("Robot sedang bergerak")

    return "Konteks persepsi robot: " + "; ".join(facts) + "."


def _clean_str(value: Any, max_len: int) -> str:
    return str(value).strip()[:max_len] if value is not None else ""


def validate_command(raw: dict) -> dict:
    """Coerce an arbitrary dict into a strictly valid robot command.

    Raises ValueError only when `emotion` itself is missing/invalid —
    every other field degrades to a safe default (the ESP32 contract
    requires emotion as the one mandatory key).
    """
    if not isinstance(raw, dict):
        raise ValueError("command must be a dict")

    emotion = _clean_str(raw.get("emotion"), 20).lower()
    if emotion not in ALLOWED_INTERNAL_EMOTIONS:
        raise ValueError(f"invalid emotion: {emotion!r}")

    command: dict[str, Any] = {"emotion": emotion, "ts": time.time()}

    animation = _clean_str(raw.get("animation"), 20).lower()
    if animation in ALLOWED_ANIMATIONS:
        command["animation"] = animation

    movement = _clean_str(raw.get("movement"), 20).lower()
    if movement in ALLOWED_MOVEMENTS:
        command["movement"] = movement

    speech = _clean_str(raw.get("speech"), config.ROBOT_SPEECH_MAX_CHARS)
    if speech:
        command["speech"] = speech

    speed = raw.get("speed")
    if isinstance(speed, (int, float)) and speed > 0:
        command["speed"] = int(max(SPEED_MIN, min(SPEED_MAX, speed)))

    duration = raw.get("duration_ms")
    if isinstance(duration, (int, float)) and duration > 0:
        command["duration_ms"] = int(max(DURATION_MIN_MS, min(DURATION_MAX_MS, duration)))

    return command


_JSON_OBJECT_RE = re.compile(r"\{.*\}", re.DOTALL)


def build_command_from_ai_text(ai_text: str) -> dict:
    """Convert an AI reply into a validated robot command.

    The expected shape is pure JSON; if the model wrapped it in prose or
    markdown fences, the first {...} block is extracted. If nothing
    usable remains, the raw text becomes the speech of a neutral command
    so the user still gets a reaction.
    """
    text = (ai_text or "").strip()
    candidate = None

    match = _JSON_OBJECT_RE.search(text)
    if match:
        try:
            parsed = json.loads(match.group(0))
            if isinstance(parsed, dict) and "emotion" in parsed:
                candidate = parsed
        except json.JSONDecodeError:
            candidate = None

    if candidate is None:
        # Non-JSON reply: wrap it safely.
        return validate_command({
            "emotion": "neutral",
            "animation": "blink",
            "movement": "stop",
            "speech": text or "...",
        })

    # AI supplied JSON: fill missing pieces so the robot always reacts.
    fallbacks = {"animation": "blink", "movement": "stop", "speech": ""}
    for key, default in fallbacks.items():
        candidate.setdefault(key, default)
    return validate_command(candidate)


FALLBACK_SPEECHES = (
    "Hmm, otakku sedang tidak bisa dihubungi. Tapi aku tetap di sini!",
    "Sinyal ke otakku terputus sebentar. Aku tetap semangat!",
    "Aku tidak bisa berpikir jelas sekarang... coba sentuh aku lagi ya!",
)


def fallback_command(reason: str = "") -> dict:
    """Safe, deterministic reaction used whenever the AI pipeline fails."""
    import random
    command = validate_command({
        "emotion": "neutral",
        "animation": "blink",
        "movement": "stop",
        "speech": random.choice(FALLBACK_SPEECHES),
    })
    command["fallback"] = True
    if reason:
        command["fallback_reason"] = str(reason)[:120]
    return command


def build_manual_command(emotion: Optional[str] = None,
                         animation: Optional[str] = None,
                         movement: Optional[str] = None,
                         speech: Optional[str] = None,
                         speed: Optional[int] = None,
                         duration_ms: Optional[int] = None) -> dict:
    """Dashboard-driven command; invalid fields are dropped, not fatal."""
    return validate_command({
        "emotion": emotion or "neutral",
        "animation": animation,
        "movement": movement or "stop",  # safe default: never move by accident
        "speech": speech,
        "speed": speed,
        "duration_ms": duration_ms,
    })
