"""Shared Flask extension instances (avoids circular imports)."""
from flask_sock import Sock

sock = Sock()
