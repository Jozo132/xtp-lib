#pragma once

// ============================================================================
// xtp_tcp_client.h — Non-blocking TCP client for W5500/STM32
//
// Provides a state-machine-driven EthernetClient wrapper that never blocks
// the main loop. Supports connect, write, read, and disconnect operations
// with configurable timeouts and automatic socket cleanup.
//
// Usage:
//   XtpTcpClient tcp;
//   tcp.onConnect([]() { Serial.println("Connected!"); });
//   tcp.onDisconnect([](const char* reason) { Serial.println(reason); });
//   tcp.onData([](const uint8_t* data, uint16_t len) { /* handle data */ });
//
//   // Connect (non-blocking, returns immediately)
//   tcp.connect(IPAddress(192,168,1,50), 3000);
//   // or: tcp.connect("192.168.1.50", 3000);
//
//   // In loop:
//   tcp.loop(ethState);
//   if (tcp.isConnected()) {
//       tcp.write("Hello", 5);
//       tcp.print("Hello\n");
//   }
// ============================================================================

#include <Arduino.h>
#include <Ethernet.h>
#include <utility/w5100.h>

// ─── Configuration Defaults ───────────────────────────────────────────────────

#ifndef XTP_TCP_CONNECT_TIMEOUT_MS
#define XTP_TCP_CONNECT_TIMEOUT_MS 5000
#endif

#ifndef XTP_TCP_RECONNECT_INTERVAL_MS
#define XTP_TCP_RECONNECT_INTERVAL_MS 5000
#endif

#ifndef XTP_TCP_IDLE_TIMEOUT_MS
#define XTP_TCP_IDLE_TIMEOUT_MS 0  // 0 = no idle timeout
#endif

#ifndef XTP_TCP_SOCKET_RELEASE_MS
#define XTP_TCP_SOCKET_RELEASE_MS 500
#endif

#ifndef XTP_TCP_ETH_STABILIZE_MS
#define XTP_TCP_ETH_STABILIZE_MS 2000
#endif

#ifndef XTP_TCP_RX_BUF_SIZE
#define XTP_TCP_RX_BUF_SIZE 512
#endif

// ─── W5500 Socket Status Values ──────────────────────────────────────────────

#define XTP_TCP_SNSR_CLOSED      0x00
#define XTP_TCP_SNSR_ESTABLISHED 0x17
#define XTP_TCP_SNSR_CLOSE_WAIT  0x1C
#define XTP_TCP_SNSR_TIME_WAIT   0x1B
#define XTP_TCP_SNSR_FIN_WAIT    0x18
#define XTP_TCP_SNSR_LAST_ACK    0x1D

// ─── Logging ─────────────────────────────────────────────────────────────────

#ifndef XTP_TCP_LOG
#define XTP_TCP_LOG(x) Serial.print(x)
#endif
#ifndef XTP_TCP_LOGLN
#define XTP_TCP_LOGLN(x) Serial.println(x)
#endif
#ifndef XTP_TCP_LOGF
#define XTP_TCP_LOGF(...) Serial.printf(__VA_ARGS__)
#endif

// ─── SPI Selection ───────────────────────────────────────────────────────────

#ifndef XTP_TCP_SPI_SELECT
  #ifdef spi_select
    #define XTP_TCP_SPI_SELECT(x)  spi_select(x)
    #define XTP_TCP_SPI_ETH        SPI_Ethernet
    #define XTP_TCP_SPI_NONE       SPI_None
  #else
    #define XTP_TCP_SPI_SELECT(x)  (void)(x)
    #define XTP_TCP_SPI_ETH        0
    #define XTP_TCP_SPI_NONE       0
  #endif
#endif

// ─── Non-blocking TCP Client Class ───────────────────────────────────────────

class XtpTcpClient {
public:
    enum State {
        TCP_IDLE = 0,       // Not connected, not trying
        TCP_CONNECTING,     // TCP connect in progress
        TCP_CONNECTED,      // Connection established, ready for I/O
        TCP_DISCONNECTING   // Graceful disconnect in progress
    };

    // Callback types
    typedef void (*ConnectCallback)();
    typedef void (*DisconnectCallback)(const char* reason);
    typedef void (*DataCallback)(const uint8_t* data, uint16_t length);

private:
    EthernetClient  _client;
    State           _state = TCP_IDLE;

    // Target
    IPAddress       _host;
    char            _hostStr[64] = {};
    uint16_t        _port = 0;
    bool            _useHostStr = false;
    bool            _hasTarget = false;
    bool            _autoReconnect = false;

    // Timing
    uint32_t        _connectStart = 0;
    uint32_t        _lastAttempt = 0;
    uint32_t        _lastActivity = 0;
    uint32_t        _disconnectTime = 0;

    // Ethernet state tracking
    uint8_t         _ethInitCycle = 0;

    // Configurable timeouts (per-instance override)
    uint32_t        _connectTimeout = XTP_TCP_CONNECT_TIMEOUT_MS;
    uint32_t        _reconnectInterval = XTP_TCP_RECONNECT_INTERVAL_MS;
    uint32_t        _idleTimeout = XTP_TCP_IDLE_TIMEOUT_MS;

    // Callbacks
    ConnectCallback     _onConnect = nullptr;
    DisconnectCallback  _onDisconnect = nullptr;
    DataCallback        _onData = nullptr;

    // ─── Socket Management ────────────────────────────────────────────────────

    uint8_t countAvailableSockets() {
        uint8_t available = 0;
        for (uint8_t sock = 0; sock < MAX_SOCK_NUM; sock++) {
            if (W5100.readSnSR(sock) == XTP_TCP_SNSR_CLOSED) {
                available++;
            }
        }
        return available;
    }

    bool tryFreeStuckSocket() {
        for (uint8_t sock = 0; sock < MAX_SOCK_NUM; sock++) {
            uint8_t status = W5100.readSnSR(sock);
            if (status == XTP_TCP_SNSR_TIME_WAIT ||
                status == XTP_TCP_SNSR_FIN_WAIT ||
                status == XTP_TCP_SNSR_CLOSE_WAIT ||
                status == XTP_TCP_SNSR_LAST_ACK) {
                XTP_TCP_LOGF("[tcp] Force-closing stuck socket %d (status=0x%02X)\n", sock, status);
                W5100.execCmdSn(sock, Sock_CLOSE);
                W5100.writeSnIR(sock, 0xFF);
                return true;
            }
        }
        return false;
    }

    void forceCloseOwnSocket() {
        uint8_t sock = _client.getSocketNumber();
        if (sock < MAX_SOCK_NUM) {
            uint8_t status = W5100.readSnSR(sock);
            if (status != XTP_TCP_SNSR_CLOSED) {
                W5100.execCmdSn(sock, Sock_CLOSE);
                W5100.writeSnIR(sock, 0xFF);
            }
        }
    }

    void disconnect(const char* reason) {
        if (reason) {
            XTP_TCP_LOGF("[tcp] %s\n", reason);
        }

        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        _client.stop();
        forceCloseOwnSocket();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

        _client = EthernetClient();
        _state = TCP_IDLE;
        _disconnectTime = millis();

        if (_onDisconnect) {
            _onDisconnect(reason);
        }
    }

    void handleIncomingData() {
        uint8_t buf[XTP_TCP_RX_BUF_SIZE];
        while (_client.available()) {
            int avail = _client.available();
            int toRead = min(avail, (int)sizeof(buf));
            int got = _client.read(buf, toRead);
            if (got > 0) {
                _lastActivity = millis();
                if (_onData) {
                    _onData(buf, (uint16_t)got);
                }
            } else {
                break;
            }
        }
    }

public:
    XtpTcpClient() = default;

    // ─── Configuration ────────────────────────────────────────────────────────

    void setConnectTimeout(uint32_t ms)   { _connectTimeout = ms; }
    void setReconnectInterval(uint32_t ms) { _reconnectInterval = ms; }
    void setIdleTimeout(uint32_t ms)      { _idleTimeout = ms; }
    void setAutoReconnect(bool enable)    { _autoReconnect = enable; }

    void onConnect(ConnectCallback cb)       { _onConnect = cb; }
    void onDisconnect(DisconnectCallback cb) { _onDisconnect = cb; }
    void onData(DataCallback cb)             { _onData = cb; }

    // ─── Connect (non-blocking, starts state machine) ─────────────────────────

    bool connect(IPAddress host, uint16_t port) {
        if (_state == TCP_CONNECTED || _state == TCP_CONNECTING) {
            close();
        }
        _host = host;
        _port = port;
        _useHostStr = false;
        _hasTarget = true;
        _lastAttempt = 0;  // Allow immediate attempt
        _disconnectTime = 0;
        _state = TCP_IDLE;  // Will transition to CONNECTING in loop()
        return true;
    }

    bool connect(const char* host, uint16_t port) {
        if (_state == TCP_CONNECTED || _state == TCP_CONNECTING) {
            close();
        }
        strncpy(_hostStr, host, sizeof(_hostStr) - 1);
        _hostStr[sizeof(_hostStr) - 1] = '\0';
        // Also try to parse as IP for faster connect
        _useHostStr = !_host.fromString(host);
        _port = port;
        _hasTarget = true;
        _lastAttempt = 0;
        _disconnectTime = 0;
        _state = TCP_IDLE;
        return true;
    }

    // ─── Close / Stop ─────────────────────────────────────────────────────────

    void close() {
        if (_state == TCP_IDLE) return;
        _hasTarget = _autoReconnect;  // Keep target if auto-reconnect
        disconnect("Connection closed by user");
    }

    void stop() {
        _hasTarget = false;
        _autoReconnect = false;
        if (_state != TCP_IDLE) {
            disconnect("Stopped");
        }
    }

    // ─── Status ───────────────────────────────────────────────────────────────

    bool isConnected() const    { return _state == TCP_CONNECTED; }
    bool isConnecting() const   { return _state == TCP_CONNECTING; }
    bool isIdle() const         { return _state == TCP_IDLE; }
    State getState() const      { return _state; }
    IPAddress remoteIP() const  { return _host; }
    uint16_t remotePort() const { return _port; }

    // Access to underlying EthernetClient (use with care)
    EthernetClient& raw() { return _client; }

    // ─── Write / Send ─────────────────────────────────────────────────────────

    size_t write(const uint8_t* data, size_t length) {
        if (_state != TCP_CONNECTED) return 0;

        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        size_t written = _client.write(data, length);
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

        if (written > 0) _lastActivity = millis();
        return written;
    }

    size_t write(uint8_t b) {
        return write(&b, 1);
    }

    size_t write(const char* str) {
        return write((const uint8_t*)str, strlen(str));
    }

    size_t print(const char* str) {
        return write((const uint8_t*)str, strlen(str));
    }

    size_t println(const char* str) {
        size_t n = print(str);
        n += write((const uint8_t*)"\r\n", 2);
        return n;
    }

    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (_state != TCP_CONNECTED) return 0;
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (len <= 0) return 0;
        if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
        return write((const uint8_t*)buf, len);
    }

    void flush() {
        if (_state != TCP_CONNECTED) return;
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        _client.flush();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
    }

    // ─── Read (manual polling, alternative to onData callback) ────────────────

    int available() {
        if (_state != TCP_CONNECTED) return 0;
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        int avail = _client.available();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
        return avail;
    }

    int read() {
        if (_state != TCP_CONNECTED) return -1;
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        int c = _client.read();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
        if (c >= 0) _lastActivity = millis();
        return c;
    }

    int read(uint8_t* buf, size_t size) {
        if (_state != TCP_CONNECTED) return 0;
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        int got = _client.read(buf, size);
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
        if (got > 0) _lastActivity = millis();
        return got;
    }

    int peek() {
        if (_state != TCP_CONNECTED) return -1;
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        int c = _client.peek();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
        return c;
    }

    // ─── Main Loop (call from loop, non-blocking) ─────────────────────────────

    template<typename EthStateT>
    void loop(EthStateT& ethState) {
        // No target configured
        if (!_hasTarget) {
            if (_state != TCP_IDLE) {
                disconnect("No target");
            }
            return;
        }

        // Ethernet not ready
        if (!ethState.isReady()) {
            if (_state != TCP_IDLE) {
                disconnect("Ethernet not ready");
            }
            return;
        }

        // Detect Ethernet reinit — sockets are invalidated
        if (ethState.initCycle != _ethInitCycle) {
            _ethInitCycle = ethState.initCycle;
            _client = EthernetClient();
            _state = TCP_IDLE;
            _lastActivity = 0;
            _disconnectTime = 0;
            _lastAttempt = millis() - _reconnectInterval + XTP_TCP_ETH_STABILIZE_MS;
            XTP_TCP_LOGLN("[tcp] Ethernet reinitialized, waiting before connect");
        }

        uint32_t now = millis();

        switch (_state) {
            case TCP_IDLE: {
                // Reconnect interval guard
                if (_lastAttempt != 0 && (now - _lastAttempt) < _reconnectInterval) return;

                // Socket release delay after disconnect
                if (_disconnectTime != 0 && (now - _disconnectTime) < XTP_TCP_SOCKET_RELEASE_MS) return;

                _lastAttempt = now;

                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);

                // Check socket availability
                uint8_t availSockets = countAvailableSockets();
                if (availSockets == 0) {
                    if (tryFreeStuckSocket()) {
                        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                        _disconnectTime = now;
                        return;
                    }
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                    XTP_TCP_LOGLN("[tcp] No sockets available");
                    return;
                }

                if (_useHostStr) {
                    XTP_TCP_LOGF("[tcp] Connecting to %s:%d ...\n", _hostStr, _port);
                } else {
                    XTP_TCP_LOGF("[tcp] Connecting to %d.%d.%d.%d:%d ...\n",
                                 _host[0], _host[1], _host[2], _host[3], _port);
                }

                _client = EthernetClient();
                _client.setTimeout(_connectTimeout);

                bool connected = _useHostStr
                    ? _client.connect(_hostStr, _port)
                    : _client.connect(_host, _port);

                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (!connected) {
                    XTP_TCP_LOGLN("[tcp] TCP connect failed");
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    _client.stop();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                    _client = EthernetClient();
                    _disconnectTime = now;

                    if (!_autoReconnect) {
                        _hasTarget = false;
                        if (_onDisconnect) _onDisconnect("Connect failed");
                    }
                    return;
                }

                _connectStart = now;
                _lastActivity = now;
                _state = TCP_CONNECTING;
                break;
            }

            case TCP_CONNECTING: {
                // Verify the socket actually reached ESTABLISHED state
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                bool alive = _client.connected();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (alive) {
                    if (_useHostStr) {
                        XTP_TCP_LOGF("[tcp] Connected to %s:%d\n", _hostStr, _port);
                    } else {
                        XTP_TCP_LOGF("[tcp] Connected to %d.%d.%d.%d:%d\n",
                                     _host[0], _host[1], _host[2], _host[3], _port);
                    }
                    _state = TCP_CONNECTED;
                    _lastActivity = now;
                    _disconnectTime = 0;
                    if (_onConnect) _onConnect();
                    return;
                }

                // Connect timeout
                if ((now - _connectStart) > _connectTimeout) {
                    disconnect("Connect timeout");
                    _lastAttempt = now;
                    if (!_autoReconnect) _hasTarget = false;
                    return;
                }
                break;
            }

            case TCP_CONNECTED: {
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                bool alive = _client.connected();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (!alive) {
                    disconnect("Connection lost");
                    _lastAttempt = now;
                    if (!_autoReconnect) _hasTarget = false;
                    return;
                }

                // Idle timeout (0 = disabled)
                if (_idleTimeout > 0 && (now - _lastActivity) > _idleTimeout) {
                    disconnect("Idle timeout");
                    _lastAttempt = now;
                    if (!_autoReconnect) _hasTarget = false;
                    return;
                }

                // Deliver incoming data via callback (if registered)
                if (_onData) {
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    handleIncomingData();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                }
                break;
            }

            case TCP_DISCONNECTING: {
                // Should not normally reach here; disconnect() goes to IDLE
                disconnect("Disconnecting");
                break;
            }

            default:
                _state = TCP_IDLE;
                break;
        }
    }

    // Convenience: loop without ethState (assumes network is ready)
    void loop() {
        struct DummyEthState {
            bool isReady() const { return true; }
            uint8_t initCycle = 0;
        };
        static DummyEthState dummy;
        loop(dummy);
    }
};
