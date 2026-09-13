# Local AI — Flask backend for AI Pet Robot V1

Flask app that hosts the AI, the robot WebSocket endpoint and the
developer dashboard. Robot code is isolated from general AI features:
`routes/robot_ws.py` + `robot_behavior.py` + `robot_state.py` are the
entire robot integration surface.

## Run

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env          # add OPENROUTER_API_KEY
.venv/bin/python run.py       # http + ws on :8765
```

| URL | Purpose |
|---|---|
| `/` | chat UI (general AI) |
| `/dashboard` | robot developer dashboard |
| `/api/health` | liveness |
| `/api/chat` | POST `{message}` → `{reply, provider}` |
| `/ws/robot` | WebSocket for the ESP32 |
| `/api/robot/status` | robot + brain snapshot (dashboard polling) |
| `/api/robot/command` | POST validated manual command |
| `/api/robot/event` | POST `{type: touch\|obstacle\|boot}` simulation |
| `/api/robot/logs` | event/command/error log stream |

## AI routing

`ai_router.py` tries the OpenRouter model chain top-down (Gemini Flash →
Groq Llama 3.1 → Mistral Nemo → Qwen 2.5 by default, configurable via
`OPENROUTER_MODELS`). First success wins; total failure raises
`AIRouterError` which the robot layer converts into a **safe fallback
command** (neutral face, blink, stop, friendly Indonesian speech).

## Robot touch flow

```
touch_head (WS) ──> robot_state.log ──> worker thread ──> ai_router.chat
                                                            │ JSON reply
              ESP32 executes <── ws.send <── validate_command
                        (AI failure → fallback_command)
```

A single in-flight guard prevents parallel AI storms; everything runs in
daemon threads so the Flask workers stay responsive.

## Tests

```bash
.venv/bin/python -m pytest tests -q     # 33 unit tests
.venv/bin/python tests/run_e2e.py       # 14-check E2E with simulated ESP32
```

The E2E test boots the real app, connects a fake ESP32 over a real
WebSocket and verifies: telemetry, boot greeting, touch→AI→command,
fallback on AI failure, malformed-JSON immunity, dashboard command
delivery and disconnect handling.

## Production note

The Flask dev server (threaded) is fine for a single-user developer
setup. For headless long-running use, put `gunicorn` with `gevent`
behind it and set `WSGI_ENV`; `flask-sock`/`simple-websocket` supports
both modes.
