"""Flask application factory for Local AI.

Wires the chat and robot blueprints, the dashboard/chat pages and the
background heartbeat monitor into a single app instance.
"""
import logging
import threading

from flask import Flask, render_template

import config
from extensions import sock
from routes.chat import chat_bp
from routes.robot_ws import robot_bp, _heartbeat_monitor

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
)


def create_app() -> Flask:
    app = Flask(__name__)
    app.config["SECRET_KEY"] = config.SECRET_KEY

    app.register_blueprint(chat_bp)
    app.register_blueprint(robot_bp)
    sock.init_app(app)

    @app.get("/")
    def index():
        return render_template("index.html")

    @app.get("/dashboard")
    def dashboard():
        return render_template("dashboard.html")

    @app.get("/api/health")
    def health():
        return {"ok": True, "service": "local-ai"}

    # Daemon thread: exits together with the process.
    threading.Thread(
        target=_heartbeat_monitor, daemon=True, name="robot-heartbeat-monitor"
    ).start()

    return app


app = create_app()

if __name__ == "__main__":
    # threaded=True keeps the websocket recv loop and REST endpoints concurrent.
    app.run(host=config.HOST, port=config.PORT, debug=config.DEBUG, threaded=True)
