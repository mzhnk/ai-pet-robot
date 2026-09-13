#!/usr/bin/env bash
# run_native_tests.sh — compile + run the protocol unit tests on the host.
#
# No Arduino toolchain needed; the protocol module is deliberately free
# of Arduino.h so the wire contract can be verified anywhere.
#
# Usage:  bash test/test_protocol/run_native_tests.sh
set -euo pipefail
cd "$(dirname "$0")/../.."          # -> esp32-firmware/

ARDUINOJSON_VER="7.0.4"
ARDUINOJSON_DIR="/tmp/ArduinoJson-${ARDUINOJSON_VER}"

if [ ! -d "$ARDUINOJSON_DIR/src" ]; then
    echo "[setup] fetching ArduinoJson v${ARDUINOJSON_VER}..."
    curl -sL -o /tmp/arduinojson.tar.gz \
        "https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v${ARDUINOJSON_VER}.tar.gz"
    tar xzf /tmp/arduinojson.tar.gz -C /tmp
fi

echo "[build] compiling native tests..."
g++ -std=c++17 -Wall -Wextra \
    -I src -I src/protocol -I "$ARDUINOJSON_DIR/src" \
    test/test_protocol/test_protocol.cpp \
    src/protocol/protocol.cpp \
    -o /tmp/test_protocol

echo "[run]"
/tmp/test_protocol
