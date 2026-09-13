"""
config.py — centralized Local AI configuration (env-driven).

All tunables live here; modules never hardcode endpoints or timeouts.
"""
import os

from dotenv import load_dotenv

load_dotenv()

# ------------------------------------------------------------
# Flask / server
# ------------------------------------------------------------
HOST = os.getenv("HOST", "0.0.0.0")
# Must match AI_WS_PORT in esp32-firmware/src/config.h (8765).
PORT = int(os.getenv("PORT", "8765"))
DEBUG = os.getenv("FLASK_DEBUG", "0") == "1"
SECRET_KEY = os.getenv("SECRET_KEY", "dev-only-change-me")

# ------------------------------------------------------------
# AI routing (OpenRouter)
# ------------------------------------------------------------
OPENROUTER_API_KEY = os.getenv("OPENROUTER_API_KEY", "")
OPENROUTER_BASE_URL = os.getenv("OPENROUTER_BASE_URL", "https://openrouter.ai/api/v1")

# Fallback chain: tried top-down until one succeeds.
# Override with OPENROUTER_MODELS="groq/llama-3.1-8b-instruct,..." if desired.
DEFAULT_MODELS = [
    "google/gemini-flash-1.5",
    "groq/llama-3.1-8b-instruct",
    "mistralai/mistral-nemo",
    "qwen/qwen-2.5-7b-instruct",
]
AI_MODELS = [m.strip() for m in os.getenv("OPENROUTER_MODELS", "").split(",") if m.strip()] or DEFAULT_MODELS

AI_REQUEST_TIMEOUT_S = float(os.getenv("AI_REQUEST_TIMEOUT_S", "8"))
AI_MAX_TOKENS = int(os.getenv("AI_MAX_TOKENS", "150"))

# ------------------------------------------------------------
# Robot integration
# ------------------------------------------------------------
# Seconds without any websocket traffic before the robot is considered offline.
ROBOT_HEARTBEAT_TIMEOUT_S = float(os.getenv("ROBOT_HEARTBEAT_TIMEOUT_S", "8"))

# Speech is clipped to match the ESP32's 128-char buffer.
ROBOT_SPEECH_MAX_CHARS = 128

# Dashboard speed slider (0-100%) maps onto the firmware's PWM clamp window
# (MOTOR_SPEED_MIN..MOTOR_SPEED_MAX in esp32-firmware/src/config.h).
ROBOT_SPEED_MIN_PWM = int(os.getenv("ROBOT_SPEED_MIN_PWM", "60"))
ROBOT_SPEED_MAX_PWM = int(os.getenv("ROBOT_SPEED_MAX_PWM", "255"))

# Simulated-event & manual command logging window.
ROBOT_LOG_MAX_ENTRIES = 300

# ------------------------------------------------------------
# V2: perception / vision
# ------------------------------------------------------------
# The ESP32-CAM unit serves its own MJPEG stream; the dashboard reaches it
# ONLY through this server's proxy endpoint (never browser -> robot).
CAMERA_STREAM_PORT = int(os.getenv("CAMERA_STREAM_PORT", "80"))
CAMERA_STREAM_TIMEOUT_S = float(os.getenv("CAMERA_STREAM_TIMEOUT_S", "5"))
CAMERA_STREAM_CHUNK = int(os.getenv("CAMERA_STREAM_CHUNK", "4096"))

# Min seconds between AI greetings for repeated person_detected events
# (the pet walks around a lot; do not burn API quota on every reappearance).
ROBOT_PERSON_GREET_COOLDOWN_S = float(os.getenv("ROBOT_PERSON_GREET_COOLDOWN_S", "45"))
