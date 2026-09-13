/**
 * connection.cpp — Wi-Fi + WebSocket connection state machine.
 *
 * Transitions (all timings from config.h):
 *   BOOT        -> CONNECTING            on begin()
 *   CONNECTING  -> CONNECTED             Wi-Fi up AND WebSocket established
 *   CONNECTING  -> OFFLINE               overall window (~10 s) expired
 *   CONNECTED   -> OFFLINE               heartbeat stale (~5 s) / socket lost / Wi-Fi lost
 *   OFFLINE     -> RECONNECTING          after a short sojourn (lets system settle)
 *   RECONNECTING-> CONNECTING            after backoff ladder (5/10/20/40/60 s)
 *
 * Reconnection never reboots and never spins in a tight loop.
 */
#include "connection.h"

#include <WiFi.h>

#include "../config.h"

// protocol.h is included by behavior for status serialization; the transport
// heartbeat here is a fixed minimal string, so no protocol include is needed.

ConnectionManager connection;
ConnectionManager* ConnectionManager::_instance = nullptr;

namespace {

const char* stateToString(ConnectionState s) {
    switch (s) {
        case ConnectionState::BOOT:         return "BOOT";
        case ConnectionState::CONNECTING:   return "CONNECTING";
        case ConnectionState::CONNECTED:    return "CONNECTED";
        case ConnectionState::OFFLINE:      return "OFFLINE";
        case ConnectionState::RECONNECTING: return "RECONNECTING";
    }
    return "UNKNOWN";
}

// Short settle time in OFFLINE before scheduling the next reconnect attempt.
constexpr uint32_t OFFLINE_SOJOURN_MS = 2000;

} // namespace

void ConnectionManager::begin(CommandHandler onCommand, StateChangeHandler onStateChange) {
    _onCommand     = onCommand;
    _onStateChange = onStateChange;
    _instance      = this;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // low latency for WebSocket
    WiFi.setAutoReconnect(false);  // we own the reconnect policy

    setState(ConnectionState::BOOT, "begin");
    enterConnecting(0);
}

void ConnectionManager::update() {
    serviceWifi();
    serviceWebsocket();
    serviceHeartbeat();

    const uint32_t now = millis();
    const uint32_t inState = now - _stateEnteredMs;

    switch (_state) {
        case ConnectionState::CONNECTING:
            if (!wifiConnected() && inState > WIFI_CONNECT_TIMEOUT_MS) {
                enterOffline("wifi join timeout");
            }
            break;

        case ConnectionState::CONNECTED:
            if (!wifiConnected()) {
                enterOffline("wifi lost");
            } else {
                // Liveness = newest of inbound traffic and accepted sends.
                // (Lib-level pongs arrive every 2 s; our own heartbeats
                // every 3 s — either proves the link is alive.)
                const uint32_t lastAlive = (_lastSendOkMs > _lastIncomingMs)
                                               ? _lastSendOkMs : _lastIncomingMs;
                if (now - lastAlive > WS_HEARTBEAT_TIMEOUT_MS) {
                    enterOffline("heartbeat timeout");
                }
            }
            break;

        case ConnectionState::OFFLINE:
            if (inState > OFFLINE_SOJOURN_MS) {
                enterReconnecting();
            }
            break;

        case ConnectionState::RECONNECTING: {
            uint8_t idx = _reconnectAttempt;
            if (idx >= WS_RECONNECT_BACKOFF_COUNT) idx = WS_RECONNECT_BACKOFF_COUNT - 1;
            const uint32_t backoff = pgm_read_dword(&WS_RECONNECT_BACKOFF_MS[idx]);
            if (inState > backoff) {
                enterConnecting(_reconnectAttempt);
            }
            break;
        }

        default:
            break;
    }
}

void ConnectionManager::sendEvent(const char* json) {
    if (json == nullptr) return;
    if (_state != ConnectionState::CONNECTED) {
        Serial.println("[conn] drop event (not connected)");
        return;
    }
    _ws.sendTXT(json);
}

bool ConnectionManager::wifiConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

int32_t ConnectionManager::rssi() const {
    return wifiConnected() ? WiFi.RSSI() : 0;
}

const char* ConnectionManager::stateName() const {
    return stateToString(_state);
}

// ------------------------------------------------------------
// State entries
// ------------------------------------------------------------

void ConnectionManager::enterConnecting(uint8_t attempt) {
    _reconnectAttempt = attempt;
    _wsStarted = false;

    if (wifiConnected()) {
        startWebsocket();
    } else {
        startWifiJoin();
    }
    setState(ConnectionState::CONNECTING,
             attempt == 0 ? "join" : "rejoin attempt");
}

void ConnectionManager::enterConnected() {
    _reconnectAttempt = 0;
    _lastIncomingMs   = millis();   // grace window starts now
    _lastSendOkMs     = millis();
    _lastHeartbeatMs  = 0;          // send first heartbeat immediately
    setState(ConnectionState::CONNECTED, "websocket established");
}

void ConnectionManager::enterOffline(const char* reason) {
    if (_wsStarted) {
        _ws.disconnect();           // stop lib-internal retries; we own them now
        _wsStarted = false;
    }
    setState(ConnectionState::OFFLINE, reason);
}

void ConnectionManager::enterReconnecting() {
    setState(ConnectionState::RECONNECTING, "backoff scheduled");
    Serial.printf("[conn] reconnect attempt %u scheduled\n", _reconnectAttempt + 1);
}

void ConnectionManager::setState(ConnectionState s, const char* reason) {
    if (_state == s) return;
    _state = s;
    _stateEnteredMs = millis();
    Serial.printf("[conn] state=%s (%s)\n", stateToString(s), reason);
    if (_onStateChange != nullptr) _onStateChange(s, reason);
}

// ------------------------------------------------------------
// Service routines
// ------------------------------------------------------------

void ConnectionManager::serviceWifi() {
    if (_wifiJoinActive && wifiConnected()) {
        _wifiJoinActive = false;
        _ip = WiFi.localIP();
        Serial.printf("[conn] wifi up ip=%u.%u.%u.%u rssi=%d\n",
                      (_ip & 0xFF), ((_ip >> 8) & 0xFF),
                      ((_ip >> 16) & 0xFF), ((_ip >> 24) & 0xFF),
                      static_cast<int>(WiFi.RSSI()));
        if (_state == ConnectionState::CONNECTING && !_wsStarted) {
            startWebsocket();
        }
    }
}

void ConnectionManager::serviceWebsocket() {
    _ws.loop();
}

void ConnectionManager::serviceHeartbeat() {
    if (_state != ConnectionState::CONNECTED) return;

    const uint32_t now = millis();
    if (_lastHeartbeatMs != 0 && now - _lastHeartbeatMs < WS_HEARTBEAT_INTERVAL_MS) return;
    _lastHeartbeatMs = now;

    // Full status heartbeat: transport keepalive + dashboard telemetry in one.
    char buf[PROTOCOL_TX_BUFFER_SIZE];
    RobotStatusFields fields{};
    if (_statusProvider != nullptr) {
        _statusProvider(fields);
    } else {
        fields.state = "UNKNOWN";
        fields.emotion = "neutral";
        fields.uptimeS = now / 1000;
        fields.rssiDbm = rssi();
        fields.wifiConnected = wifiConnected();
        fields.wsConnected = true;
    }
    const size_t len = protocol_serialize_status(buf, sizeof(buf), fields);
    if (len > 0) {
        if (_ws.sendTXT(buf, len)) {
            _lastSendOkMs = now;    // kernel accepted the frame: link healthy
        }
    }
}

void ConnectionManager::startWifiJoin() {
    Serial.printf("[conn] joining SSID=%s\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    _wifiJoinStartMs = millis();
    _wifiJoinActive  = true;
    (void)_wifiJoinStartMs;   // retained for future diagnostics
}

void ConnectionManager::startWebsocket() {
    Serial.printf("[conn] ws begin %s:%u%s\n", AI_WS_HOST, AI_WS_PORT, AI_WS_PATH);
    _ws.begin(AI_WS_HOST, AI_WS_PORT, AI_WS_PATH);
    _ws.onEvent(ConnectionManager::websocketEventStatic);
    // The FSM owns reconnection policy; the lib only gets a slow safety net
    // so it can never spin in a tight retry loop behind our back.
    _ws.setReconnectInterval(15000);
    // Frequent protocol pings keep _lastIncomingMs fresh within the 5 s
    // heartbeat-timeout window even when the AI server is silent.
    _ws.enableHeartbeat(2000, 1000, 3);
    _wsStarted = true;
}

// ------------------------------------------------------------
// WebSocket events
// ------------------------------------------------------------

void ConnectionManager::websocketEventStatic(WStype_t type, uint8_t* payload, size_t length) {
    if (_instance != nullptr) {
        _instance->onWebsocketEvent(type, payload, length);
    }
}

void ConnectionManager::onWebsocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[conn] websocket connected");
            enterConnected();
            break;

        case WStype_DISCONNECTED:
            Serial.println("[conn] websocket disconnected");
            if (_state == ConnectionState::CONNECTED) {
                enterOffline("ws disconnected");
            }
            break;

        case WStype_TEXT:
            _lastIncomingMs = millis();
            if (_onCommand != nullptr) {
                _onCommand(reinterpret_cast<const char*>(payload), length);
            }
            break;

        case WStype_PONG:
            _lastIncomingMs = millis();
            break;

        case WStype_ERROR:
            Serial.println("[conn] websocket error");
            break;

        default:
            break;
    }
}
