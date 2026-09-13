"""
routes/chat.py — general-purpose AI chat (the core Local AI feature).

Completely independent from the robot integration: plain chat text in,
plain chat text out, routed through ai_router's provider fallback chain.
"""
import logging

from flask import Blueprint, jsonify, request

from ai_router import AIRouterError, ai_router

logger = logging.getLogger(__name__)

chat_bp = Blueprint("chat", __name__)

# Very small rolling memory (last N turns) so the chat feels continuous.
_HISTORY_LIMIT = 12
_chat_history: list = []


@chat_bp.post("/api/chat")
def api_chat():
    body = request.get_json(silent=True) or {}
    message = str(body.get("message", "")).strip()
    if not message:
        return jsonify({"ok": False, "error": "message is required"}), 400

    _chat_history.append({"role": "user", "content": message})
    messages = [{"role": "system",
                 "content": "Kamu asisten AI lokal yang ramah. Jawab ringkas dan jelas."}]
    messages += _chat_history[-_HISTORY_LIMIT:]

    try:
        reply, provider = ai_router.chat(messages)
    except AIRouterError as exc:
        logger.warning("chat failed: %s", exc)
        return jsonify({
            "ok": False,
            "error": "Semua provider AI gagal. Periksa OPENROUTER_API_KEY / koneksi.",
            "detail": str(exc),
        }), 502

    _chat_history.append({"role": "assistant", "content": reply})
    return jsonify({"ok": True, "reply": reply, "provider": provider})


@chat_bp.post("/api/chat/clear")
def api_chat_clear():
    _chat_history.clear()
    return jsonify({"ok": True})
