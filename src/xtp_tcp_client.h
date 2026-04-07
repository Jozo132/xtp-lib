#pragma once

// ============================================================================
// xtp_tcp_client.h — Non-blocking TCP client for W5500/STM32
//
// Provides a state-machine-driven EthernetClient wrapper that never blocks
// the main loop. Supports connect, write, read, and disconnect operations
// with configurable timeouts and automatic socket cleanup.
//
// Two modes:
//   KEEP_ALIVE  — Connect once, stay connected, send/receive freely
//   SINGLE_SHOT — Connect, send, optionally wait for response, disconnect
//
// Transaction queue (up to 8 slots):
//   int8_t id = tcp.send(host, port, data, len);   // Returns 0-7 or -1
//   TxState s = tcp.txState(id);                    // Track async progress
//   tcp.txCancel(id);                               // Cancel a pending tx
//
// Keep-alive usage:
//   XtpTcpClient tcp;
//   tcp.setKeepAlive(true);
//   tcp.onConnect([]() { Serial.println("Connected!"); });
//   tcp.onData([](const uint8_t* data, uint16_t len) { /* handle */ });
//   tcp.connect(IPAddress(192,168,1,50), 3000);
//   // In loop:
//   tcp.loop(ethState);
//   if (tcp.isConnected()) tcp.write("Hello", 5);
//
// Single-shot / transaction usage:
//   XtpTcpClient tcp;                               // keepAlive=false default
//   int8_t id = tcp.send(IPAddress(192,168,1,50), 3000, "Hello\n", 6);
//   // In loop:
//   tcp.loop(ethState);
//   if (tcp.txState(id) == XtpTcpClient::TX_DONE_OK) { /* success */ }
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

// Maximum payload per transaction slot
#ifndef XTP_TCP_TX_BUF_SIZE
#define XTP_TCP_TX_BUF_SIZE 256
#endif

// Number of transaction queue slots (max 8)
#ifndef XTP_TCP_TX_QUEUE_SIZE
#define XTP_TCP_TX_QUEUE_SIZE 8
#endif

// Default time to wait for a response after sending (single-shot mode)
#ifndef XTP_TCP_TX_RESPONSE_TIMEOUT_MS
#define XTP_TCP_TX_RESPONSE_TIMEOUT_MS 2000
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
    // Connection state machine
    enum State {
        TCP_IDLE = 0,       // Not connected, not trying
        TCP_CONNECTING,     // TCP connect in progress
        TCP_CONNECTED,      // Connection established, ready for I/O
        TCP_DISCONNECTING   // Graceful disconnect in progress
    };

    // Transaction lifecycle states (like setTimeout returning an ID)
    enum TxState : uint8_t {
        TX_FREE = 0,        // Slot is available for reuse
        TX_PENDING,         // Queued, waiting for connection/turn
        TX_CONNECTING,      // TCP connect in progress for this tx
        TX_SENDING,         // Writing payload to socket
        TX_WAIT_RESPONSE,   // Payload sent, waiting for response data
        TX_DONE_OK,         // Completed successfully (terminal — call txRelease/txState auto-clears)
        TX_DONE_FAIL,       // Failed (timeout, connect error, etc.) (terminal)
        TX_CANCELLED        // Cancelled by user (terminal)
    };

    // Callback types
    typedef void (*ConnectCallback)();
    typedef void (*DisconnectCallback)(const char* reason);
    typedef void (*DataCallback)(const uint8_t* data, uint16_t length);
    typedef void (*TxDoneCallback)(int8_t id, TxState result, const uint8_t* response, uint16_t responseLen);

private:
    // ─── Transaction slot ─────────────────────────────────────────────────────
    struct TxSlot {
        TxState     state = TX_FREE;
        IPAddress   host;
        char        hostStr[64] = {};     // Hostname or IP string
        bool        useHostStr = false;   // true = connect via hostStr
        uint16_t    port = 0;
        uint8_t     payload[XTP_TCP_TX_BUF_SIZE];
        uint16_t    payloadLen = 0;
        uint32_t    startTime = 0;
        uint32_t    sendTime = 0;         // When payload was fully written
        uint32_t    timeout = XTP_TCP_CONNECT_TIMEOUT_MS;
        uint32_t    responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS;
        bool        waitForResponse = false;
        TxDoneCallback callback = nullptr;
        // Response buffer (optional, reuses RX buf via pointer)
        uint8_t     response[XTP_TCP_RX_BUF_SIZE];
        uint16_t    responseLen = 0;

        void reset() {
            state = TX_FREE;
            payloadLen = 0;
            responseLen = 0;
            callback = nullptr;
            waitForResponse = false;
            useHostStr = false;
            hostStr[0] = '\0';
        }
    };

    EthernetClient  _client;
    State           _state = TCP_IDLE;

    // Target (for keep-alive mode)
    IPAddress       _host;
    char            _hostStr[64] = {};
    uint16_t        _port = 0;
    bool            _useHostStr = false;
    bool            _hasTarget = false;
    bool            _autoReconnect = false;
    bool            _keepAlive = false;    // false = single-shot (default)

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

    // Callbacks (keep-alive mode)
    ConnectCallback     _onConnect = nullptr;
    DisconnectCallback  _onDisconnect = nullptr;
    DataCallback        _onData = nullptr;

    // Transaction queue
    TxSlot          _txSlots[XTP_TCP_TX_QUEUE_SIZE];
    int8_t          _activeTx = -1;  // Index of currently executing transaction (-1 = none)

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

    // ─── Transaction Internals ────────────────────────────────────────────────

    // Find next pending tx (FIFO order by startTime)
    int8_t findNextPendingTx() {
        int8_t best = -1;
        uint32_t bestTime = 0xFFFFFFFF;
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            if (_txSlots[i].state == TX_PENDING && _txSlots[i].startTime < bestTime) {
                best = i;
                bestTime = _txSlots[i].startTime;
            }
        }
        return best;
    }

    // Find a free slot
    int8_t findFreeSlot() {
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            if (_txSlots[i].state == TX_FREE) return i;
        }
        return -1;
    }

    // Complete a transaction
    void completeTx(int8_t id, TxState result) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) return;
        TxSlot& slot = _txSlots[id];
        TxDoneCallback cb = slot.callback;
        const uint8_t* resp = slot.response;
        uint16_t respLen = slot.responseLen;
        slot.state = result;
        _activeTx = -1;

        // Disconnect after single-shot tx completes
        if (!_keepAlive && _state != TCP_IDLE) {
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
            _client.stop();
            forceCloseOwnSocket();
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
            _client = EthernetClient();
            _state = TCP_IDLE;
            _disconnectTime = millis();
        }

        if (cb) cb(id, result, resp, respLen);
    }

    // Fail all pending/active transactions (e.g. on disconnect)
    void failAllActiveTx(const char* reason) {
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            TxState s = _txSlots[i].state;
            if (s == TX_CONNECTING || s == TX_SENDING || s == TX_WAIT_RESPONSE) {
                TxDoneCallback cb = _txSlots[i].callback;
                _txSlots[i].state = TX_DONE_FAIL;
                if (cb) cb(i, TX_DONE_FAIL, nullptr, 0);
            }
        }
        _activeTx = -1;
    }

    // Process transaction state machine (called from loop when not in keep-alive,
    // or also in keep-alive mode if transactions are queued)
    void processTxQueue() {
        uint32_t now = millis();

        // If there's an active transaction, process it
        if (_activeTx >= 0) {
            TxSlot& slot = _txSlots[_activeTx];

            switch (slot.state) {
                case TX_CONNECTING: {
                    // Check if connection established
                    if (_state == TCP_CONNECTED) {
                        // Connection ready — send payload
                        slot.state = TX_SENDING;
                    } else if (_state == TCP_IDLE) {
                        // Connection failed before we got connected
                        XTP_TCP_LOGF("[tcp] tx[%d] connect failed\n", _activeTx);
                        completeTx(_activeTx, TX_DONE_FAIL);
                    } else if (now - slot.startTime > slot.timeout) {
                        XTP_TCP_LOGF("[tcp] tx[%d] connect timeout\n", _activeTx);
                        completeTx(_activeTx, TX_DONE_FAIL);
                    }
                    return;
                }

                case TX_SENDING: {
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    size_t written = _client.write(slot.payload, slot.payloadLen);
                    _client.flush();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                    if (written == slot.payloadLen) {
                        _lastActivity = now;
                        slot.sendTime = now;
                        if (slot.waitForResponse) {
                            slot.state = TX_WAIT_RESPONSE;
                            slot.responseLen = 0;
                            XTP_TCP_LOGF("[tcp] tx[%d] sent %u bytes, waiting for response\n",
                                         _activeTx, slot.payloadLen);
                        } else {
                            XTP_TCP_LOGF("[tcp] tx[%d] sent %u bytes, done\n",
                                         _activeTx, slot.payloadLen);
                            completeTx(_activeTx, TX_DONE_OK);
                        }
                    } else {
                        XTP_TCP_LOGF("[tcp] tx[%d] write failed (%u/%u)\n",
                                     _activeTx, (unsigned)written, slot.payloadLen);
                        completeTx(_activeTx, TX_DONE_FAIL);
                    }
                    return;
                }

                case TX_WAIT_RESPONSE: {
                    // Read any available response data
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    while (_client.available() && slot.responseLen < sizeof(slot.response)) {
                        int avail = _client.available();
                        int space = sizeof(slot.response) - slot.responseLen;
                        int toRead = min(avail, space);
                        int got = _client.read(&slot.response[slot.responseLen], toRead);
                        if (got > 0) {
                            slot.responseLen += got;
                            _lastActivity = now;
                        } else {
                            break;
                        }
                    }
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                    // Got some data — consider it done
                    if (slot.responseLen > 0) {
                        XTP_TCP_LOGF("[tcp] tx[%d] received %u byte response\n",
                                     _activeTx, slot.responseLen);
                        completeTx(_activeTx, TX_DONE_OK);
                        return;
                    }

                    // Response timeout
                    if (now - slot.sendTime > slot.responseTimeout) {
                        XTP_TCP_LOGF("[tcp] tx[%d] response timeout\n", _activeTx);
                        completeTx(_activeTx, TX_DONE_FAIL);
                        return;
                    }

                    // Connection lost while waiting
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    bool alive = _client.connected();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                    if (!alive) {
                        // If we got data before disconnect, that's still OK
                        if (slot.responseLen > 0) {
                            completeTx(_activeTx, TX_DONE_OK);
                        } else {
                            XTP_TCP_LOGF("[tcp] tx[%d] disconnected while waiting\n", _activeTx);
                            completeTx(_activeTx, TX_DONE_FAIL);
                        }
                    }
                    return;
                }

                default:
                    // Terminal state in active slot — shouldn't happen, clean up
                    _activeTx = -1;
                    break;
            }
        }

        // No active transaction — pick next pending one
        int8_t next = findNextPendingTx();
        if (next < 0) return;  // Queue empty

        TxSlot& slot = _txSlots[next];
        _activeTx = next;

        // In keep-alive mode: if already connected to same host:port, skip connect
        if (_keepAlive && _state == TCP_CONNECTED) {
            // Check if target matches current connection
            bool sameTarget = (slot.port == _port);
            if (sameTarget) {
                if (slot.useHostStr) {
                    sameTarget = _useHostStr && strcmp(slot.hostStr, _hostStr) == 0;
                } else {
                    sameTarget = !_useHostStr && slot.host == _host;
                }
            }
            if (sameTarget) {
                slot.state = TX_SENDING;
                XTP_TCP_LOGF("[tcp] tx[%d] reusing keep-alive connection\n", next);
                return;
            }
            // Different target — need to reconnect
            disconnect("Switching target for tx");
        }

        // Need to connect (or already connecting for keep-alive)
        if (_state == TCP_IDLE) {
            slot.state = TX_CONNECTING;
            slot.startTime = now;
            _port = slot.port;
            if (slot.useHostStr) {
                strncpy(_hostStr, slot.hostStr, sizeof(_hostStr) - 1);
                _hostStr[sizeof(_hostStr) - 1] = '\0';
                _useHostStr = !_host.fromString(_hostStr);
            } else {
                _host = slot.host;
                _useHostStr = false;
            }
            _hasTarget = true;
            _lastAttempt = 0;
            _disconnectTime = 0;
            if (slot.useHostStr) {
                XTP_TCP_LOGF("[tcp] tx[%d] initiating connection to %s:%d\n",
                             next, slot.hostStr, slot.port);
            } else {
                XTP_TCP_LOGF("[tcp] tx[%d] initiating connection to %d.%d.%d.%d:%d\n",
                             next, slot.host[0], slot.host[1], slot.host[2], slot.host[3], slot.port);
            }
        } else if (_state == TCP_CONNECTED) {
            slot.state = TX_SENDING;
        } else {
            // Already connecting — wait
            slot.state = TX_CONNECTING;
            if (slot.startTime == 0) slot.startTime = now;
        }
    }

public:
    XtpTcpClient() = default;

    // ─── Configuration ────────────────────────────────────────────────────────

    void setConnectTimeout(uint32_t ms)    { _connectTimeout = ms; }
    void setReconnectInterval(uint32_t ms) { _reconnectInterval = ms; }
    void setIdleTimeout(uint32_t ms)       { _idleTimeout = ms; }
    void setAutoReconnect(bool enable)     { _autoReconnect = enable; }
    void setKeepAlive(bool enable)         { _keepAlive = enable; }

    void onConnect(ConnectCallback cb)       { _onConnect = cb; }
    void onDisconnect(DisconnectCallback cb) { _onDisconnect = cb; }
    void onData(DataCallback cb)             { _onData = cb; }

    // ─── Keep-Alive Connect (non-blocking, starts state machine) ──────────────

    bool connect(IPAddress host, uint16_t port) {
        if (_state == TCP_CONNECTED || _state == TCP_CONNECTING) {
            close();
        }
        _host = host;
        _port = port;
        _useHostStr = false;
        _hasTarget = true;
        _keepAlive = true;  // Explicit connect implies keep-alive
        _lastAttempt = 0;
        _disconnectTime = 0;
        _state = TCP_IDLE;
        return true;
    }

    bool connect(const char* host, uint16_t port) {
        if (_state == TCP_CONNECTED || _state == TCP_CONNECTING) {
            close();
        }
        strncpy(_hostStr, host, sizeof(_hostStr) - 1);
        _hostStr[sizeof(_hostStr) - 1] = '\0';
        _useHostStr = !_host.fromString(host);
        _port = port;
        _hasTarget = true;
        _keepAlive = true;
        _lastAttempt = 0;
        _disconnectTime = 0;
        _state = TCP_IDLE;
        return true;
    }

    // ─── Transaction Queue API (fire-and-forget / single-shot) ────────────────
    //
    // Returns slot ID (0..7) on success, -1 if queue full.
    // Like JS setTimeout() — you get an ID back to track or cancel.

    int8_t send(IPAddress host, uint16_t port,
                const uint8_t* data, uint16_t len,
                TxDoneCallback callback = nullptr,
                bool waitForResponse = false,
                uint32_t responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS) {
        int8_t id = findFreeSlot();
        if (id < 0) {
            XTP_TCP_LOGLN("[tcp] tx queue full");
            return -1;
        }
        if (len > XTP_TCP_TX_BUF_SIZE) {
            XTP_TCP_LOGF("[tcp] tx payload too large (%u > %u)\n", len, (unsigned)XTP_TCP_TX_BUF_SIZE);
            return -1;
        }

        TxSlot& slot = _txSlots[id];
        slot.reset();
        slot.state = TX_PENDING;
        slot.host = host;
        slot.port = port;
        memcpy(slot.payload, data, len);
        slot.payloadLen = len;
        slot.startTime = millis();
        slot.timeout = _connectTimeout;
        slot.responseTimeout = responseTimeout;
        slot.waitForResponse = waitForResponse;
        slot.callback = callback;

        XTP_TCP_LOGF("[tcp] tx[%d] queued %u bytes to %d.%d.%d.%d:%d%s\n",
                     id, len, host[0], host[1], host[2], host[3], port,
                     waitForResponse ? " (await response)" : "");
        return id;
    }

    // Convenience: send a string (IPAddress)
    int8_t send(IPAddress host, uint16_t port, const char* str,
                TxDoneCallback callback = nullptr,
                bool waitForResponse = false,
                uint32_t responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS) {
        return send(host, port, (const uint8_t*)str, strlen(str),
                    callback, waitForResponse, responseTimeout);
    }

    // Send with hostname/IP string + binary data
    int8_t send(const char* host, uint16_t port,
                const uint8_t* data, uint16_t len,
                TxDoneCallback callback = nullptr,
                bool waitForResponse = false,
                uint32_t responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS) {
        int8_t id = findFreeSlot();
        if (id < 0) {
            XTP_TCP_LOGLN("[tcp] tx queue full");
            return -1;
        }
        if (len > XTP_TCP_TX_BUF_SIZE) {
            XTP_TCP_LOGF("[tcp] tx payload too large (%u > %u)\n", len, (unsigned)XTP_TCP_TX_BUF_SIZE);
            return -1;
        }

        TxSlot& slot = _txSlots[id];
        slot.reset();
        slot.state = TX_PENDING;
        strncpy(slot.hostStr, host, sizeof(slot.hostStr) - 1);
        slot.hostStr[sizeof(slot.hostStr) - 1] = '\0';
        // Try to parse as IP — if it works, store in slot.host too for fast comparison
        slot.useHostStr = !slot.host.fromString(host);
        slot.port = port;
        memcpy(slot.payload, data, len);
        slot.payloadLen = len;
        slot.startTime = millis();
        slot.timeout = _connectTimeout;
        slot.responseTimeout = responseTimeout;
        slot.waitForResponse = waitForResponse;
        slot.callback = callback;

        XTP_TCP_LOGF("[tcp] tx[%d] queued %u bytes to %s:%d%s\n",
                     id, len, host, port,
                     waitForResponse ? " (await response)" : "");
        return id;
    }

    // Send with hostname/IP string + string data
    int8_t send(const char* host, uint16_t port, const char* str,
                TxDoneCallback callback = nullptr,
                bool waitForResponse = false,
                uint32_t responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS) {
        return send(host, port, (const uint8_t*)str, (uint16_t)strlen(str),
                    callback, waitForResponse, responseTimeout);
    }

    // ─── Transaction State Query ──────────────────────────────────────────────

    // Check transaction state by ID (like checking a promise)
    // Terminal states (TX_DONE_OK, TX_DONE_FAIL, TX_CANCELLED) are returned
    // once and then auto-released to TX_FREE on the NEXT call.
    TxState txState(int8_t id) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) return TX_FREE;
        TxState s = _txSlots[id].state;
        // Auto-release terminal states after being read
        if (s == TX_DONE_OK || s == TX_DONE_FAIL || s == TX_CANCELLED) {
            _txSlots[id].reset();
        }
        return s;
    }

    // Peek at state without auto-releasing
    TxState txPeek(int8_t id) const {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) return TX_FREE;
        return _txSlots[id].state;
    }

    // Get response data for a completed transaction (before calling txState which frees it)
    uint16_t txResponse(int8_t id, uint8_t* buf, uint16_t maxLen) const {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) return 0;
        const TxSlot& slot = _txSlots[id];
        if (slot.state != TX_DONE_OK) return 0;
        uint16_t copyLen = min(slot.responseLen, maxLen);
        if (copyLen > 0 && buf) memcpy(buf, slot.response, copyLen);
        return copyLen;
    }

    // Explicitly release a slot (for terminal states you've finished with)
    void txRelease(int8_t id) {
        if (id >= 0 && id < XTP_TCP_TX_QUEUE_SIZE) {
            _txSlots[id].reset();
            if (_activeTx == id) _activeTx = -1;
        }
    }

    // Cancel a pending/in-progress transaction
    bool txCancel(int8_t id) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) return false;
        TxSlot& slot = _txSlots[id];
        if (slot.state == TX_FREE || slot.state == TX_DONE_OK ||
            slot.state == TX_DONE_FAIL || slot.state == TX_CANCELLED) return false;

        XTP_TCP_LOGF("[tcp] tx[%d] cancelled\n", id);
        bool wasActive = (_activeTx == id);
        slot.state = TX_CANCELLED;
        TxDoneCallback cb = slot.callback;
        if (wasActive) {
            _activeTx = -1;
            // If single-shot and this was the active tx, disconnect
            if (!_keepAlive && _state != TCP_IDLE) {
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                _client.stop();
                forceCloseOwnSocket();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                _client = EthernetClient();
                _state = TCP_IDLE;
                _disconnectTime = millis();
            }
        }
        if (cb) cb(id, TX_CANCELLED, nullptr, 0);
        return true;
    }

    // Number of active + pending transactions
    uint8_t txPending() const {
        uint8_t count = 0;
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            TxState s = _txSlots[i].state;
            if (s == TX_PENDING || s == TX_CONNECTING || s == TX_SENDING || s == TX_WAIT_RESPONSE) {
                count++;
            }
        }
        return count;
    }

    // Number of free slots
    uint8_t txFreeSlots() const {
        uint8_t count = 0;
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            if (_txSlots[i].state == TX_FREE) count++;
        }
        return count;
    }

    // ─── Close / Stop ─────────────────────────────────────────────────────────

    void close() {
        if (_state == TCP_IDLE && _activeTx < 0) return;
        failAllActiveTx("Connection closed by user");
        _hasTarget = _autoReconnect && _keepAlive;
        disconnect("Connection closed by user");
    }

    void stop() {
        failAllActiveTx("Stopped");
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
    bool isKeepAlive() const    { return _keepAlive; }
    State getState() const      { return _state; }
    IPAddress remoteIP() const  { return _host; }
    uint16_t remotePort() const { return _port; }

    // Access to underlying EthernetClient (use with care)
    EthernetClient& raw() { return _client; }

    // ─── Write / Send (keep-alive direct I/O) ─────────────────────────────────

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
        // Ethernet not ready — fail everything
        if (!ethState.isReady()) {
            if (_state != TCP_IDLE) {
                failAllActiveTx("Ethernet not ready");
                disconnect("Ethernet not ready");
            }
            return;
        }

        // Detect Ethernet reinit — sockets are invalidated
        if (ethState.initCycle != _ethInitCycle) {
            _ethInitCycle = ethState.initCycle;
            failAllActiveTx("Ethernet reinitialized");
            _client = EthernetClient();
            _state = TCP_IDLE;
            _lastActivity = 0;
            _disconnectTime = 0;
            _lastAttempt = millis() - _reconnectInterval + XTP_TCP_ETH_STABILIZE_MS;
            XTP_TCP_LOGLN("[tcp] Ethernet reinitialized, waiting before connect");
            return;
        }

        uint32_t now = millis();

        // Determine if we need to be connected:
        // - Keep-alive with target: always try to stay connected
        // - Transaction pending: need connection for the tx target
        bool hasPendingTx = (_activeTx >= 0) || (findNextPendingTx() >= 0);
        bool needConnection = (_keepAlive && _hasTarget) || hasPendingTx;

        // Process connection state machine
        switch (_state) {
            case TCP_IDLE: {
                if (!needConnection) return;

                // Reconnect interval guard
                if (_lastAttempt != 0 && (now - _lastAttempt) < _reconnectInterval) {
                    // Still process tx queue for timeout checks
                    if (hasPendingTx) processTxQueue();
                    return;
                }

                // Socket release delay after disconnect
                if (_disconnectTime != 0 && (now - _disconnectTime) < XTP_TCP_SOCKET_RELEASE_MS) {
                    return;
                }

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

                    if (!_keepAlive && !hasPendingTx) {
                        _hasTarget = false;
                        if (_onDisconnect) _onDisconnect("Connect failed");
                    }
                    // Let tx queue handle its own timeouts
                    if (hasPendingTx) processTxQueue();
                    return;
                }

                _connectStart = now;
                _lastActivity = now;
                _state = TCP_CONNECTING;
                break;
            }

            case TCP_CONNECTING: {
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
                    // Process tx queue now that we're connected
                    if (hasPendingTx) processTxQueue();
                    return;
                }

                // Connect timeout
                if ((now - _connectStart) > _connectTimeout) {
                    failAllActiveTx("Connect timeout");
                    disconnect("Connect timeout");
                    _lastAttempt = now;
                    if (!_keepAlive && !_autoReconnect) _hasTarget = false;
                    return;
                }
                break;
            }

            case TCP_CONNECTED: {
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                bool alive = _client.connected();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (!alive) {
                    failAllActiveTx("Connection lost");
                    disconnect("Connection lost");
                    _lastAttempt = now;
                    if (!_keepAlive && !_autoReconnect) _hasTarget = false;
                    return;
                }

                // Idle timeout (0 = disabled, only in keep-alive without pending tx)
                if (_keepAlive && _idleTimeout > 0 && !hasPendingTx &&
                    (now - _lastActivity) > _idleTimeout) {
                    disconnect("Idle timeout");
                    _lastAttempt = now;
                    return;
                }

                // Process transaction queue
                if (hasPendingTx) {
                    processTxQueue();
                }

                // Deliver incoming data via callback (keep-alive, no active tx consuming data)
                if (_onData && _activeTx < 0) {
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    handleIncomingData();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                }
                break;
            }

            case TCP_DISCONNECTING: {
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
