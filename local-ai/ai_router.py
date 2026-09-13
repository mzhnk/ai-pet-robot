"""
ai_router.py — AI provider routing with an automatic fallback chain.

Tries the configured OpenRouter models top-down; the first model that
answers wins. All failures are recorded so the robot layer can degrade
gracefully and the dashboard can show "brain status".

This module is deliberately robot-agnostic: it is plain text-in,
text-out AI routing usable by any Local AI feature.
"""
import logging
import threading
import time
from typing import Any, Optional

import requests

import config

logger = logging.getLogger(__name__)


class AIRouterError(Exception):
    """Raised when every configured provider fails."""


class AIRouter:
    def __init__(self, api_key: str = "", models: Optional[list] = None):
        self._api_key = api_key or config.OPENROUTER_API_KEY
        self._models = models or list(config.AI_MODELS)
        self._lock = threading.Lock()
        self._last_provider: Optional[str] = None
        self._last_error: Optional[str] = None
        self._last_success_ts: Optional[float] = None

    # ------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------

    def chat(self, messages: list, timeout_s: Optional[float] = None) -> tuple[str, str]:
        """
        Send `messages` (OpenAI-style) through the fallback chain.

        Returns (reply_text, provider_model). Raises AIRouterError when
        no provider answers.
        """
        if not self._api_key:
            self._record_error("OPENROUTER_API_KEY not configured")
            raise AIRouterError(self._last_error)

        timeout = timeout_s or config.AI_REQUEST_TIMEOUT_S
        errors = []

        for model in self._models:
            try:
                reply = self._call_openrouter(model, messages, timeout)
                self._record_success(model)
                return reply, model
            except (requests.RequestException, KeyError, ValueError) as exc:
                short = f"{model}: {type(exc).__name__}: {exc}"[:200]
                logger.warning("AI provider failed (%s)", short)
                errors.append(short)

        self._record_error(" | ".join(errors) if errors else "no providers configured")
        raise AIRouterError(self._last_error)

    def brain_status(self) -> dict:
        """Health snapshot for the dashboard."""
        with self._lock:
            healthy = self._last_error is None and self._last_success_ts is not None
            return {
                "ok": healthy,
                "configured": bool(self._api_key),
                "models": list(self._models),
                "last_provider": self._last_provider,
                "last_error": self._last_error,
                "last_success_ts": self._last_success_ts,
            }

    # ------------------------------------------------------------
    # Internals
    # ------------------------------------------------------------

    def _call_openrouter(self, model: str, messages: list, timeout: float) -> str:
        url = f"{config.OPENROUTER_BASE_URL}/chat/completions"
        headers = {
            "Authorization": f"Bearer {self._api_key}",
            "Content-Type": "application/json",
        }
        payload: dict[str, Any] = {
            "model": model,
            "messages": messages,
            "max_tokens": config.AI_MAX_TOKENS,
            "temperature": 0.7,
        }
        resp = requests.post(url, headers=headers, json=payload, timeout=timeout)
        resp.raise_for_status()
        data = resp.json()
        content = data["choices"][0]["message"]["content"]
        if not isinstance(content, str) or not content.strip():
            raise ValueError("empty AI reply")
        return content.strip()

    def _record_success(self, model: str) -> None:
        with self._lock:
            self._last_provider = model
            self._last_error = None
            self._last_success_ts = time.time()

    def _record_error(self, message: str) -> None:
        with self._lock:
            self._last_error = message


# Shared instance for the whole Local AI app.
ai_router = AIRouter()
