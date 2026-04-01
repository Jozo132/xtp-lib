#pragma once

// ============================================================================
// xtp_ws_client.h — Outgoing WebSocket client for W5500/STM32
//
// Connects to a WebSocket server and maintains the connection with automatic
// reconnection, socket cleanup, and ping/pong handling.
//
// URL format: ws://host:port/path   (default port 80, default path /)
// Example:    ws://192.168.1.50:9001/ws
//
// Usage:
//   XtpWsClient wsClient;
//   wsClient.setUrl("ws://192.168.1.50:9091/ws");
//   wsClient.onConnect([]() { Serial.println("Connected!"); });
//   wsClient.onDisconnect([]() { Serial.println("Disconnected!"); });
//   // In loop:
//   wsClient.loop();
//   if (wsClient.isConnected()) {
//       wsClient.sendBinary(data, length);
//   }
// ============================================================================

#include <Arduino.h>
#include <Ethernet.h>
#include <utility/w5100.h>  // For W5100 class socket control

// ─── Configuration Defaults ───────────────────────────────────────────────────

// Minimum milliseconds between connection attempts
#ifndef XTP_WS_RECONNECT_INTERVAL_MS
#define XTP_WS_RECONNECT_INTERVAL_MS 5000
#endif

// Receive buffer for handshake response
#ifndef XTP_WS_HANDSHAKE_BUF_SIZE
#define XTP_WS_HANDSHAKE_BUF_SIZE 512
#endif

// Timeout for no server activity (pings, data) — detects dead connections
#ifndef XTP_WS_ACTIVITY_TIMEOUT_MS
#define XTP_WS_ACTIVITY_TIMEOUT_MS 90000  // 90 seconds
#endif

// Time to wait after disconnect before reconnecting (socket release)
#ifndef XTP_WS_SOCKET_RELEASE_MS
#define XTP_WS_SOCKET_RELEASE_MS 500
#endif

// Time to wait after Ethernet reinit before connecting
#ifndef XTP_WS_ETH_STABILIZE_MS
#define XTP_WS_ETH_STABILIZE_MS 2000
#endif

// TCP connect timeout
#ifndef XTP_WS_CONNECT_TIMEOUT_MS
#define XTP_WS_CONNECT_TIMEOUT_MS 5000
#endif

// Handshake timeout
#ifndef XTP_WS_HANDSHAKE_TIMEOUT_MS
#define XTP_WS_HANDSHAKE_TIMEOUT_MS 3000
#endif

// ─── W5500 Socket Status Values ───────────────────────────────────────────────

#define XTP_SNSR_CLOSED      0x00
#define XTP_SNSR_INIT        0x13
#define XTP_SNSR_LISTEN      0x14
#define XTP_SNSR_ESTABLISHED 0x17
#define XTP_SNSR_CLOSE_WAIT  0x1C
#define XTP_SNSR_TIME_WAIT   0x1B
#define XTP_SNSR_FIN_WAIT    0x18
#define XTP_SNSR_LAST_ACK    0x1D

// ─── Logging Macros ───────────────────────────────────────────────────────────

#ifndef XTP_WS_LOG
#define XTP_WS_LOG(x) Serial.print(x)
#endif
#ifndef XTP_WS_LOGLN
#define XTP_WS_LOGLN(x) Serial.println(x)
#endif
#ifndef XTP_WS_LOGF
#define XTP_WS_LOGF(...) Serial.printf(__VA_ARGS__)
#endif

// ─── SPI Selection (for shared SPI bus with W5500) ────────────────────────────

#ifndef XTP_WS_SPI_SELECT
  #ifdef spi_select
    #define XTP_WS_SPI_SELECT(x)  spi_select(x)
    #define XTP_WS_SPI_ETH        SPI_Ethernet
    #define XTP_WS_SPI_NONE       SPI_None
  #else
    #define XTP_WS_SPI_SELECT(x)  (void)(x)
    #define XTP_WS_SPI_ETH        0
    #define XTP_WS_SPI_NONE       0
  #endif
#endif

// ─── WebSocket Client Class ───────────────────────────────────────────────────

class XtpWsClient {
public:
    enum State {
        WS_IDLE = 0,
        WS_CONNECTING,
        WS_HANDSHAKE,
        WS_CONNECTED
    };

    // Callback types
    typedef void (*ConnectCallback)();
    typedef void (*DisconnectCallback)(const char* reason);
    typedef void (*MessageCallback)(const uint8_t* data, uint16_t length);

private:
    EthernetClient  _client;
    State           _state = WS_IDLE;
    
    // Timing
    uint32_t        _lastAttempt = 0;
    uint32_t        _connectStart = 0;
    uint32_t        _lastActivity = 0;
    uint32_t        _disconnectTime = 0;
    
    // Ethernet state tracking
    uint8_t         _ethInitCycle = 0;
    
    // URL components
    char            _host[64] = {};
    uint16_t        _port = 80;
    char            _path[64] = "/";
    bool            _hasUrl = false;
    
    // Handshake buffer
    char            _handshakeBuf[XTP_WS_HANDSHAKE_BUF_SIZE];
    int             _handshakeLen = 0;
    
    // Callbacks
    ConnectCallback     _onConnect = nullptr;
    DisconnectCallback  _onDisconnect = nullptr;
    MessageCallback     _onMessage = nullptr;
    
    // Fixed mask key for client frames (deterministic; masking is for proxies, not security)
    static constexpr uint8_t MASK_KEY[4] = { 0x37, 0xC1, 0x4A, 0x8B };

    // ─── Socket Management ────────────────────────────────────────────────────

    uint8_t countAvailableSockets() {
        uint8_t available = 0;
        for (uint8_t sock = 0; sock < MAX_SOCK_NUM; sock++) {
            if (W5100.readSnSR(sock) == XTP_SNSR_CLOSED) {
                available++;
            }
        }
        return available;
    }

    void printSocketStatus() {
        XTP_WS_LOG("[ws] Sockets: ");
        for (uint8_t sock = 0; sock < MAX_SOCK_NUM; sock++) {
            uint8_t status = W5100.readSnSR(sock);
            XTP_WS_LOGF("%d=0x%02X ", sock, status);
        }
        XTP_WS_LOGLN("");
    }

    // Force-close a socket stuck in TCP close sequence
    bool tryFreeStuckSocket(bool desperate = false) {
        // First pass: sockets stuck in closing states
        for (uint8_t sock = 0; sock < MAX_SOCK_NUM; sock++) {
            uint8_t status = W5100.readSnSR(sock);
            if (status == XTP_SNSR_TIME_WAIT || 
                status == XTP_SNSR_FIN_WAIT || 
                status == XTP_SNSR_CLOSE_WAIT ||
                status == XTP_SNSR_LAST_ACK) {
                XTP_WS_LOGF("[ws] Force-closing stuck socket %d (status=0x%02X)\n", sock, status);
                W5100.execCmdSn(sock, Sock_CLOSE);
                W5100.writeSnIR(sock, 0xFF);
                return true;
            }
        }
        
        // Second pass: desperate mode - close ESTABLISHED sockets
        if (desperate) {
            for (uint8_t sock = 0; sock < MAX_SOCK_NUM; sock++) {
                uint8_t status = W5100.readSnSR(sock);
                if (status == XTP_SNSR_ESTABLISHED) {
                    XTP_WS_LOGF("[ws] Force-closing ESTABLISHED socket %d (desperate)\n", sock);
                    W5100.execCmdSn(sock, Sock_CLOSE);
                    W5100.writeSnIR(sock, 0xFF);
                    return true;
                }
            }
        }
        return false;
    }

    void forceCloseSocket(uint8_t sock) {
        if (sock < MAX_SOCK_NUM) {
            uint8_t status = W5100.readSnSR(sock);
            if (status != XTP_SNSR_CLOSED) {
                XTP_WS_LOGF("[ws] Force-closing socket %d (status=0x%02X)\n", sock, status);
                W5100.execCmdSn(sock, Sock_CLOSE);
                W5100.writeSnIR(sock, 0xFF);
            }
        }
    }

    // ─── URL Parsing ──────────────────────────────────────────────────────────

    bool parseUrl(const char* url) {
        if (!url || strlen(url) < 6) return false;
        const char* p = url;
        if (strncmp(p, "ws://", 5) != 0) return false;
        p += 5;

        // Extract host
        const char* hostStart = p;
        while (*p && *p != ':' && *p != '/') p++;
        int hostLen = p - hostStart;
        if (hostLen <= 0 || hostLen >= (int)sizeof(_host)) return false;
        strncpy(_host, hostStart, hostLen);
        _host[hostLen] = '\0';

        // Extract optional port
        _port = 80;
        if (*p == ':') {
            p++;
            _port = (uint16_t)atoi(p);
            while (*p && *p != '/') p++;
        }

        // Extract optional path
        if (*p == '/') {
            strncpy(_path, p, sizeof(_path) - 1);
            _path[sizeof(_path) - 1] = '\0';
        } else {
            strcpy(_path, "/");
        }

        _hasUrl = true;
        return true;
    }

    // ─── Handshake ────────────────────────────────────────────────────────────

    void resetHandshakeBuffer() {
        _handshakeLen = 0;
        _handshakeBuf[0] = '\0';
    }

    // Returns: 1=success, 0=incomplete, -1=failed
    int readHandshakeResponse() {
        while (_client.available() && _handshakeLen < (int)sizeof(_handshakeBuf) - 1) {
            _handshakeBuf[_handshakeLen++] = (char)_client.read();
        }
        _handshakeBuf[_handshakeLen] = '\0';

        if (strstr(_handshakeBuf, "\r\n\r\n")) {
            bool ok = (strstr(_handshakeBuf, "101") != nullptr);
            resetHandshakeBuffer();
            return ok ? 1 : -1;
        }
        return 0;
    }

    // ─── Frame Handling ───────────────────────────────────────────────────────

    bool sendPong(const uint8_t* payload, uint8_t length) {
        if (_state != WS_CONNECTED) return false;
        
        uint8_t frame[6 + 125];
        if (length > 125) length = 125;
        frame[0] = 0x8A;  // FIN + opcode pong
        frame[1] = 0x80 | length;  // MASK + length
        memcpy(&frame[2], MASK_KEY, 4);
        for (uint8_t i = 0; i < length; i++) {
            frame[6 + i] = payload[i] ^ MASK_KEY[i & 3];
        }
        return _client.write(frame, 6 + length) == (6 + length);
    }

    // Returns false if connection should be closed
    bool handleIncoming() {
        bool receivedAny = false;
        
        while (_client.available() >= 2) {
            receivedAny = true;
            
            uint8_t hdr[2];
            if (_client.read(hdr, 2) != 2) break;

            uint8_t opcode = hdr[0] & 0x0F;
            bool masked = (hdr[1] & 0x80) != 0;
            uint16_t payloadLen = hdr[1] & 0x7F;

            // Extended length
            if (payloadLen == 126) {
                uint8_t ext[2];
                if (_client.read(ext, 2) != 2) return true;
                payloadLen = ((uint16_t)ext[0] << 8) | ext[1];
            } else if (payloadLen == 127) {
                uint8_t ext[8];
                _client.read(ext, 8);
                return true;  // Can't handle 64-bit length
            }

            // Read mask key if present
            uint8_t maskKey[4] = {0};
            if (masked) {
                if (_client.read(maskKey, 4) != 4) return true;
            }

            // Read payload
            uint8_t payload[256];
            uint16_t toRead = min(payloadLen, (uint16_t)sizeof(payload));
            uint16_t got = 0;
            if (toRead > 0) {
                got = _client.read(payload, toRead);
            }
            
            // Skip remaining payload beyond buffer
            if (payloadLen > toRead) {
                uint8_t discard[32];
                uint16_t remaining = payloadLen - toRead;
                while (remaining > 0) {
                    uint16_t chunk = min(remaining, (uint16_t)sizeof(discard));
                    _client.read(discard, chunk);
                    remaining -= chunk;
                }
            }

            // Unmask if needed
            if (masked) {
                for (uint16_t i = 0; i < got; i++) {
                    payload[i] ^= maskKey[i & 3];
                }
            }

            switch (opcode) {
                case 0x9:  // Ping
                    sendPong(payload, (uint8_t)got);
                    break;
                case 0xA:  // Pong - ignore
                    break;
                case 0x8:  // Close
                    return false;
                case 0x1:  // Text
                case 0x2:  // Binary
                    if (_onMessage && got > 0) {
                        _onMessage(payload, got);
                    }
                    break;
                default:
                    break;
            }
        }
        
        if (receivedAny) {
            _lastActivity = millis();
        }
        return true;
    }

    // ─── Disconnect Helper ────────────────────────────────────────────────────

    void disconnect(const char* reason) {
        if (reason) {
            XTP_WS_LOGF("[ws] %s\n", reason);
        }
        resetHandshakeBuffer();
        
        XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
        
        uint8_t sock = _client.getSocketNumber();
        _client.stop();
        forceCloseSocket(sock);
        
        XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
        
        _client = EthernetClient();
        _state = WS_IDLE;
        _disconnectTime = millis();
        
        if (_onDisconnect) {
            _onDisconnect(reason);
        }
    }

public:
    XtpWsClient() = default;

    // ─── Configuration ────────────────────────────────────────────────────────

    bool setUrl(const char* url) {
        return parseUrl(url);
    }

    void clearUrl() {
        _hasUrl = false;
        _host[0] = '\0';
    }

    void onConnect(ConnectCallback cb) { _onConnect = cb; }
    void onDisconnect(DisconnectCallback cb) { _onDisconnect = cb; }
    void onMessage(MessageCallback cb) { _onMessage = cb; }

    // ─── Status ───────────────────────────────────────────────────────────────

    bool isConnected() const { return _state == WS_CONNECTED; }
    State getState() const { return _state; }
    const char* getHost() const { return _host; }
    uint16_t getPort() const { return _port; }
    const char* getPath() const { return _path; }

    // ─── Sending Data ─────────────────────────────────────────────────────────

    bool sendBinary(const void* payload, uint16_t length) {
        if (_state != WS_CONNECTED) return false;

        uint8_t header[8];
        int headerLen;

        header[0] = 0x82;  // FIN=1, opcode=2 (binary)

        if (length <= 125) {
            header[1] = 0x80 | (uint8_t)length;
            memcpy(&header[2], MASK_KEY, 4);
            headerLen = 6;
        } else {
            header[1] = 0x80 | 126;
            header[2] = (length >> 8) & 0xFF;
            header[3] = length & 0xFF;
            memcpy(&header[4], MASK_KEY, 4);
            headerLen = 8;
        }

        XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
        
        if (_client.write(header, headerLen) != headerLen) {
            XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
            return false;
        }

        // Write masked payload in chunks
        const uint8_t* data = (const uint8_t*)payload;
        uint16_t sent = 0;
        uint8_t chunk[256];
        while (sent < length) {
            uint16_t chunkLen = min((uint16_t)(length - sent), (uint16_t)sizeof(chunk));
            for (uint16_t i = 0; i < chunkLen; i++) {
                chunk[i] = data[sent + i] ^ MASK_KEY[(sent + i) & 3];
            }
            uint16_t written = _client.write(chunk, chunkLen);
            if (written == 0) {
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
                return false;
            }
            sent += written;
        }

        XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
        return true;
    }

    bool sendText(const char* text) {
        if (_state != WS_CONNECTED || !text) return false;
        
        uint16_t length = strlen(text);
        uint8_t header[8];
        int headerLen;

        header[0] = 0x81;  // FIN=1, opcode=1 (text)

        if (length <= 125) {
            header[1] = 0x80 | (uint8_t)length;
            memcpy(&header[2], MASK_KEY, 4);
            headerLen = 6;
        } else {
            header[1] = 0x80 | 126;
            header[2] = (length >> 8) & 0xFF;
            header[3] = length & 0xFF;
            memcpy(&header[4], MASK_KEY, 4);
            headerLen = 8;
        }

        XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
        
        if (_client.write(header, headerLen) != headerLen) {
            XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
            return false;
        }

        // Write masked payload
        const uint8_t* data = (const uint8_t*)text;
        uint16_t sent = 0;
        uint8_t chunk[256];
        while (sent < length) {
            uint16_t chunkLen = min((uint16_t)(length - sent), (uint16_t)sizeof(chunk));
            for (uint16_t i = 0; i < chunkLen; i++) {
                chunk[i] = data[sent + i] ^ MASK_KEY[(sent + i) & 3];
            }
            uint16_t written = _client.write(chunk, chunkLen);
            if (written == 0) {
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
                return false;
            }
            sent += written;
        }

        XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
        return true;
    }

    // ─── Main Loop ────────────────────────────────────────────────────────────

    // Call this from your main loop. Requires ethState for link/ready checks.
    // Template allows any EthState-like struct with isReady() and initCycle.
    template<typename EthStateT>
    void loop(EthStateT& ethState) {
        // No URL configured
        if (!_hasUrl) {
            if (_state != WS_IDLE) {
                disconnect("URL cleared");
            }
            return;
        }

        // Ethernet not ready
        if (!ethState.isReady()) {
            if (_state != WS_IDLE) {
                disconnect("Ethernet not ready");
            }
            return;
        }

        // Detect Ethernet reinit - sockets are invalidated
        if (ethState.initCycle != _ethInitCycle) {
            _ethInitCycle = ethState.initCycle;
            _client = EthernetClient();
            resetHandshakeBuffer();
            _state = WS_IDLE;
            _lastActivity = 0;
            _disconnectTime = 0;
            _lastAttempt = millis() - XTP_WS_RECONNECT_INTERVAL_MS + XTP_WS_ETH_STABILIZE_MS;
            XTP_WS_LOGLN("[ws] Ethernet reinitialized, waiting before connect");
        }

        uint32_t now = millis();

        switch (_state) {
            case WS_IDLE: {
                // Reconnect interval
                if ((now - _lastAttempt) < XTP_WS_RECONNECT_INTERVAL_MS) return;
                
                // Socket release delay
                if (_disconnectTime != 0 && (now - _disconnectTime) < XTP_WS_SOCKET_RELEASE_MS) return;
                
                _lastAttempt = now;

                XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                
                // Check link status
                EthernetLinkStatus linkStat = Ethernet.linkStatus();
                
                // Check socket availability
                uint8_t availSockets = countAvailableSockets();
                if (availSockets == 0) {
                    printSocketStatus();
                    
                    if (tryFreeStuckSocket(false)) {
                        XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
                        _disconnectTime = now;
                        return;
                    }
                    
                    if (tryFreeStuckSocket(true)) {
                        XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
                        _disconnectTime = now;
                        return;
                    }
                    
                    XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
                    XTP_WS_LOGLN("[ws] No sockets available");
                    return;
                }
                
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);

                if (linkStat != LinkON) {
                    XTP_WS_LOGLN("[ws] Link not ready");
                    return;
                }

                XTP_WS_LOGF("[ws] Connecting to %s:%d%s (sockets=%d)...\n", _host, _port, _path, availSockets);
                
                _client = EthernetClient();
                
                XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                _client.setTimeout(XTP_WS_CONNECT_TIMEOUT_MS);
                
                IPAddress ip;
                bool resolved = ip.fromString(_host);
                bool connected = resolved ? _client.connect(ip, _port) : _client.connect(_host, _port);
                
                uint8_t sockIdx = _client.getSocketNumber();
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);

                if (!connected) {
                    XTP_WS_LOGF("[ws] TCP connect failed (sock=%d)\n", sockIdx);
                    XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                    _client.stop();
                    XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);
                    _client = EthernetClient();
                    _disconnectTime = now;
                    return;
                }

                // Send HTTP upgrade request
                XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                _client.printf(
                    "GET %s HTTP/1.1\r\n"
                    "Host: %s:%d\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                    "Sec-WebSocket-Version: 13\r\n"
                    "\r\n",
                    _path, _host, _port
                );
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);

                _connectStart = now;
                resetHandshakeBuffer();
                _state = WS_HANDSHAKE;
                break;
            }

            case WS_HANDSHAKE: {
                if ((now - _connectStart) > XTP_WS_HANDSHAKE_TIMEOUT_MS) {
                    disconnect("Handshake timeout");
                    _lastAttempt = now;
                    return;
                }

                XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                int hsResult = readHandshakeResponse();
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);

                if (hsResult == 1) {
                    XTP_WS_LOGF("[ws] Connected to %s:%d%s\n", _host, _port, _path);
                    _state = WS_CONNECTED;
                    _lastActivity = now;
                    _disconnectTime = 0;
                    if (_onConnect) _onConnect();
                } else if (hsResult == -1) {
                    disconnect("Handshake rejected");
                    _lastAttempt = now;
                }
                break;
            }

            case WS_CONNECTED: {
                XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                bool alive = _client.connected();
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);

                if (!alive) {
                    disconnect("Connection lost");
                    _lastAttempt = now;
                    return;
                }

                // Activity timeout
                if ((now - _lastActivity) > XTP_WS_ACTIVITY_TIMEOUT_MS) {
                    disconnect("Activity timeout");
                    _lastAttempt = now;
                    return;
                }

                // Handle incoming frames
                XTP_WS_SPI_SELECT(XTP_WS_SPI_ETH);
                bool wsOk = handleIncoming();
                XTP_WS_SPI_SELECT(XTP_WS_SPI_NONE);

                if (!wsOk) {
                    disconnect("Server closed connection");
                    _lastAttempt = now;
                    return;
                }
                break;
            }

            default:
                _state = WS_IDLE;
                break;
        }
    }

    // Simplified loop for when ethState is not applicable (always ready)
    void loop() {
        struct DummyEthState {
            bool isReady() const { return true; }
            uint8_t initCycle = 0;
        };
        static DummyEthState dummy;
        loop(dummy);
    }

    // Manual disconnect
    void close() {
        if (_state != WS_IDLE) {
            disconnect("Manual close");
        }
    }
};

// Static member initialization
constexpr uint8_t XtpWsClient::MASK_KEY[4];
