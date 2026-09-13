/**
 * connection.h — Connection FSM: BOOT -> CONNECTING -> CONNECTED -> OFFLINE -> RECONNECTING.
 *
 * Deliberately separate from the Behavior FSM. Responsibilities:
 *  - Wi-Fi station management
 *  - WebSocket transport to the Local AI laptop
 *  - Heartbeat emission + freshness supervision
 *  - Reconnect with exponential backoff (5/10/20/40/60 s), never a tight loop
 */
#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

#include "../protocol/protocol.h"

enum class ConnectionState : uint8_t {
    BOOT = 0,
    CONNECTING,
    CONNECTED,
    OFFLINE,
    RECONNECTING,
};

class ConnectionManager {
public:
    /// Handler invoked for every text payload arriving from the Local AI.
    using CommandHandler = void (*)(const char* payload, size_t length);
    using StateChangeHandler = void (*)(ConnectionState newState, const char* reason);
    /// Fills the status fields for the periodic heartbeat (wired to behavior).
    using StatusProvider = void (*)(RobotStatusFields& fields);

    void begin(CommandHandler onCommand, StateChangeHandler onStateChange);
    void setStatusProvider(StatusProvider provider) { _statusProvider = provider; }
    void update();

    void sendEvent(const char* json);

    bool        isOnline() const { return _state == ConnectionState::CONNECTED; }
    ConnectionState state() const { return _state; }
    const char* stateName() const;
    int32_t     rssi() const;
    uint32_t    ip() const { return _ip; }
    bool        wifiConnected() const;

private:
    void enterConnecting(uint8_t attempt);
    void enterConnected();
    void enterOffline(const char* reason);
    void enterReconnecting();
    void setState(ConnectionState s, const char* reason);

    void serviceWifi();
    void serviceWebsocket();
    void serviceHeartbeat();
    void startWifiJoin();
    void startWebsocket();

    static void websocketEventStatic(WStype_t type, uint8_t* payload, size_t length);
    void onWebsocketEvent(WStype_t type, uint8_t* payload, size_t length);

    WebSocketsClient _ws;
    CommandHandler        _onCommand     = nullptr;
    StateChangeHandler    _onStateChange = nullptr;
    StatusProvider        _statusProvider = nullptr;

    ConnectionState _state = ConnectionState::BOOT;

    uint32_t _stateEnteredMs = 0;
    uint32_t _lastIncomingMs = 0;
    uint32_t _lastSendOkMs = 0;     // last accepted outgoing frame (liveness)
    uint32_t _lastHeartbeatMs = 0;
    uint32_t _wifiJoinStartMs = 0;
    uint32_t _ip = 0;
    bool     _wifiJoinActive = false;
    bool     _wsStarted = false;
    uint8_t  _reconnectAttempt = 0;

    static ConnectionManager* _instance;
};

extern ConnectionManager connection;
