#!/usr/bin/env python3
"""run.py — convenience entrypoint for Local AI (python run.py)."""
from app import app

if __name__ == "__main__":
    import config

    app.run(host=config.HOST, port=config.PORT, debug=config.DEBUG, threaded=True)
