#pragma once

// ============================================================================
// xtp_tcp_client.h — Non-blocking TCP client for W5500/STM32
//
// Provides a non-blocking EthernetClient-like wrapper that never blocks the
// main loop. By default, connect/write/print/println/stop build a queued
// single-shot request that loop() dispatches in the background using any free
// W5500 socket. For long-lived sockets, enable keep-alive explicitly.
//
// Queued client-style usage (default):
//   XtpTcpClient tcp;
//   tcp.connect(IPAddress(192,168,1,50), 3000);
//   tcp.println("Hello");
//   tcp.stop();
//   // In loop: ethernet_loop() services the client automatically.
//   // Call setQueuedWaitForResponse(true) to expose queued replies via
//   // available()/read()/peek().
//
// Explicit keep-alive usage:
//   XtpTcpClient tcp;
//   tcp.setKeepAlive(true);
//   tcp.onConnect([]() { Serial.println("Connected!"); });
//   tcp.onData([](const uint8_t* data, uint16_t len) { /* handle */ });
//   tcp.connect(IPAddress(192,168,1,50), 3000);
//   // In loop: ethernet_loop() services the client automatically.
//   if (tcp.isConnected()) tcp.write("Hello", 5);
//
// Advanced queue access is still available through send()/txState()/txCancel().
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

#ifndef XTP_TCP_CLIENT_INSTANCE_MAX
#define XTP_TCP_CLIENT_INSTANCE_MAX 8
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
        TCP_IDLE = 0,
        TCP_CONNECTING,
        TCP_CONNECTED,
        TCP_DISCONNECTING
    };

    enum TxState : uint8_t {
        TX_FREE = 0,
        TX_BUILDING,
        TX_PENDING,
        TX_CONNECTING,
        TX_SENDING,
        TX_WAIT_RESPONSE,
        TX_DONE_OK,
        TX_DONE_FAIL,
        TX_CANCELLED
    };

    typedef void (*ConnectCallback)();
    typedef void (*DisconnectCallback)(const char* reason);
    typedef void (*DataCallback)(const uint8_t* data, uint16_t length);
    typedef void (*TxDoneCallback)(int8_t id, TxState result, const uint8_t* response, uint16_t responseLen);

private:
    struct Registry {
        XtpTcpClient* instances[XTP_TCP_CLIENT_INSTANCE_MAX] = {};
    };

    struct TxSlot {
        TxState     state = TX_FREE;
        IPAddress   host;
        char        hostStr[64] = {};
        bool        useHostStr = false;
        uint16_t    port = 0;
        uint8_t     payload[XTP_TCP_TX_BUF_SIZE];
        uint16_t    payloadLen = 0;
        uint32_t    startTime = 0;
        uint32_t    sendTime = 0;
        uint32_t    completeTime = 0;
        uint32_t    timeout = XTP_TCP_CONNECT_TIMEOUT_MS;
        uint32_t    responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS;
        bool        waitForResponse = false;
        bool        frontend = false;
        TxDoneCallback callback = nullptr;
        uint8_t     response[XTP_TCP_RX_BUF_SIZE];
        uint16_t    responseLen = 0;
        uint16_t    readOffset = 0;

        void reset() {
            state = TX_FREE;
            host = IPAddress();
            hostStr[0] = '\0';
            useHostStr = false;
            port = 0;
            payloadLen = 0;
            startTime = 0;
            sendTime = 0;
            completeTime = 0;
            timeout = XTP_TCP_CONNECT_TIMEOUT_MS;
            responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS;
            waitForResponse = false;
            frontend = false;
            callback = nullptr;
            responseLen = 0;
            readOffset = 0;
        }
    };

    struct TxWorker {
        EthernetClient client;
        int8_t         txId = -1;
        uint32_t       connectStart = 0;
        uint32_t       lastActivity = 0;
        uint16_t       bytesSent = 0;
    };

    EthernetClient  _client;
    State           _state = TCP_IDLE;

    IPAddress       _host;
    char            _hostStr[64] = {};
    uint16_t        _port = 0;
    bool            _useHostStr = false;
    bool            _hasTarget = false;
    bool            _autoReconnect = false;
    bool            _keepAlive = false;

    uint32_t        _connectStart = 0;
    uint32_t        _lastAttempt = 0;
    uint32_t        _lastActivity = 0;
    uint32_t        _disconnectTime = 0;

    uint8_t         _ethInitCycle = 0;

    uint32_t        _connectTimeout = XTP_TCP_CONNECT_TIMEOUT_MS;
    uint32_t        _reconnectInterval = XTP_TCP_RECONNECT_INTERVAL_MS;
    uint32_t        _idleTimeout = XTP_TCP_IDLE_TIMEOUT_MS;
    bool            _queuedWaitForResponse = false;
    uint32_t        _queuedResponseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS;
    bool            _registered = false;

    ConnectCallback     _onConnect = nullptr;
    DisconnectCallback  _onDisconnect = nullptr;
    DataCallback        _onData = nullptr;

    TxSlot          _txSlots[XTP_TCP_TX_QUEUE_SIZE];
    TxWorker        _txWorkers[MAX_SOCK_NUM];
    int8_t          _frontTx = -1;

    static Registry& registry() {
        static Registry value;
        return value;
    }

    void registerSelf() {
        if (_registered) {
            return;
        }

        Registry& value = registry();
        for (uint8_t i = 0; i < XTP_TCP_CLIENT_INSTANCE_MAX; i++) {
            if (value.instances[i] == this) {
                _registered = true;
                return;
            }
        }

        for (uint8_t i = 0; i < XTP_TCP_CLIENT_INSTANCE_MAX; i++) {
            if (value.instances[i] == nullptr) {
                value.instances[i] = this;
                _registered = true;
                return;
            }
        }

        XTP_TCP_LOGLN("[tcp] client registry full");
    }

    void unregisterSelf() {
        if (!_registered) {
            return;
        }

        Registry& value = registry();
        for (uint8_t i = 0; i < XTP_TCP_CLIENT_INSTANCE_MAX; i++) {
            if (value.instances[i] == this) {
                value.instances[i] = nullptr;
                break;
            }
        }
        _registered = false;
    }

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

    void forceCloseClientSocket(EthernetClient& client) {
        uint8_t sock = client.getSocketNumber();
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
        forceCloseClientSocket(_client);
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

    bool isTerminalState(TxState state) const {
        return state == TX_DONE_OK || state == TX_DONE_FAIL || state == TX_CANCELLED;
    }

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

    int8_t findFreeSlot() {
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            if (_txSlots[i].state == TX_FREE) {
                return i;
            }
        }
        return -1;
    }

    int8_t findIdleWorker() {
        for (uint8_t i = 0; i < MAX_SOCK_NUM; i++) {
            if (_txWorkers[i].txId < 0) {
                return (int8_t)i;
            }
        }
        return -1;
    }

    int8_t findWorkerByTx(int8_t id) {
        for (uint8_t i = 0; i < MAX_SOCK_NUM; i++) {
            if (_txWorkers[i].txId == id) {
                return (int8_t)i;
            }
        }
        return -1;
    }

    int8_t findNextReadableFrontendTx() const {
        int8_t best = -1;
        uint32_t bestTime = 0xFFFFFFFF;
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            const TxSlot& slot = _txSlots[i];
            if (!slot.frontend || slot.state != TX_DONE_OK) {
                continue;
            }
            if (slot.responseLen <= slot.readOffset) {
                continue;
            }
            if (slot.completeTime < bestTime) {
                best = i;
                bestTime = slot.completeTime;
            }
        }
        return best;
    }

    bool hasFrontendTxInFlight() const {
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            const TxSlot& slot = _txSlots[i];
            if (!slot.frontend) {
                continue;
            }
            if (slot.state == TX_PENDING ||
                slot.state == TX_CONNECTING ||
                slot.state == TX_SENDING ||
                slot.state == TX_WAIT_RESPONSE) {
                return true;
            }
        }
        return false;
    }

    void clearWorker(uint8_t workerIndex) {
        _txWorkers[workerIndex].client = EthernetClient();
        _txWorkers[workerIndex].txId = -1;
        _txWorkers[workerIndex].connectStart = 0;
        _txWorkers[workerIndex].lastActivity = 0;
        _txWorkers[workerIndex].bytesSent = 0;
    }

    void stopWorker(uint8_t workerIndex) {
        TxWorker& worker = _txWorkers[workerIndex];
        if (worker.txId < 0) {
            clearWorker(workerIndex);
            return;
        }

        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        worker.client.stop();
        forceCloseClientSocket(worker.client);
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
        clearWorker(workerIndex);
    }

    void completeTx(int8_t id, TxState result) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return;
        }

        TxSlot& slot = _txSlots[id];
        TxDoneCallback cb = slot.callback;
        const uint8_t* resp = slot.response;
        uint16_t respLen = slot.responseLen;
        int8_t workerIndex = findWorkerByTx(id);

        if (workerIndex >= 0) {
            stopWorker((uint8_t)workerIndex);
        }

        slot.state = result;
        slot.completeTime = millis();

        if (cb) {
            cb(id, result, resp, respLen);
        }

        if (slot.frontend && (result != TX_DONE_OK || respLen == 0 || !slot.waitForResponse)) {
            if (_frontTx == id) {
                _frontTx = -1;
            }
            slot.reset();
        }
    }

    void failRunningWorkers(const char* reason) {
        for (uint8_t i = 0; i < MAX_SOCK_NUM; i++) {
            if (_txWorkers[i].txId < 0) {
                continue;
            }
            int8_t txId = _txWorkers[i].txId;
            if (reason) {
                XTP_TCP_LOGF("[tcp] tx[%d] %s\n", txId, reason);
            }
            completeTx(txId, TX_DONE_FAIL);
        }
    }

    bool startTxOnWorker(uint8_t workerIndex, int8_t txId, uint32_t now) {
        if (txId < 0 || txId >= XTP_TCP_TX_QUEUE_SIZE) {
            return false;
        }

        TxSlot& slot = _txSlots[txId];
        TxWorker& worker = _txWorkers[workerIndex];
        if (slot.state != TX_PENDING) {
            return false;
        }

        clearWorker(workerIndex);
        worker.txId = txId;
        worker.connectStart = now;
        worker.lastActivity = now;
        worker.bytesSent = 0;
        slot.state = TX_CONNECTING;

        if (slot.useHostStr) {
            XTP_TCP_LOGF("[tcp] tx[%d] initiating connection to %s:%d\n",
                         txId, slot.hostStr, slot.port);
        } else {
            XTP_TCP_LOGF("[tcp] tx[%d] initiating connection to %d.%d.%d.%d:%d\n",
                         txId, slot.host[0], slot.host[1], slot.host[2], slot.host[3], slot.port);
        }

        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        worker.client = EthernetClient();
        worker.client.setTimeout(slot.timeout);
        bool started = slot.useHostStr
            ? worker.client.connect(slot.hostStr, slot.port)
            : worker.client.connect(slot.host, slot.port);
        bool alive = started && worker.client.connected();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

        if (!started) {
            XTP_TCP_LOGF("[tcp] tx[%d] connect failed\n", txId);
            completeTx(txId, TX_DONE_FAIL);
            return false;
        }

        if (alive) {
            slot.state = TX_SENDING;
        }
        return true;
    }

    void processWorker(uint8_t workerIndex, uint32_t now) {
        TxWorker& worker = _txWorkers[workerIndex];
        if (worker.txId < 0) {
            return;
        }

        int8_t txId = worker.txId;
        TxSlot& slot = _txSlots[txId];

        switch (slot.state) {
            case TX_CONNECTING: {
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                bool alive = worker.client.connected();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (alive) {
                    slot.state = TX_SENDING;
                    worker.lastActivity = now;
                    return;
                }
                if ((now - worker.connectStart) > slot.timeout) {
                    XTP_TCP_LOGF("[tcp] tx[%d] connect timeout\n", txId);
                    completeTx(txId, TX_DONE_FAIL);
                }
                return;
            }

            case TX_SENDING: {
                size_t remaining = slot.payloadLen - worker.bytesSent;
                if (remaining == 0) {
                    slot.sendTime = now;
                    if (slot.waitForResponse) {
                        slot.state = TX_WAIT_RESPONSE;
                        slot.responseLen = 0;
                        slot.readOffset = 0;
                        worker.lastActivity = now;
                    } else {
                        XTP_TCP_LOGF("[tcp] tx[%d] sent %u bytes, done\n", txId, slot.payloadLen);
                        completeTx(txId, TX_DONE_OK);
                    }
                    return;
                }

                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                size_t written = worker.client.write(slot.payload + worker.bytesSent, remaining);
                bool alive = worker.client.connected();
                if (written > 0 && (worker.bytesSent + written) >= slot.payloadLen) {
                    worker.client.flush();
                }
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (written > 0) {
                    worker.bytesSent += written;
                    worker.lastActivity = now;
                    if (worker.bytesSent >= slot.payloadLen) {
                        slot.sendTime = now;
                        if (slot.waitForResponse) {
                            slot.state = TX_WAIT_RESPONSE;
                            slot.responseLen = 0;
                            slot.readOffset = 0;
                            XTP_TCP_LOGF("[tcp] tx[%d] sent %u bytes, waiting for response\n",
                                         txId, slot.payloadLen);
                        } else {
                            XTP_TCP_LOGF("[tcp] tx[%d] sent %u bytes, done\n",
                                         txId, slot.payloadLen);
                            completeTx(txId, TX_DONE_OK);
                        }
                    }
                    return;
                }

                if (!alive || (now - worker.lastActivity) > slot.timeout) {
                    XTP_TCP_LOGF("[tcp] tx[%d] write failed (%u/%u)\n",
                                 txId, (unsigned)worker.bytesSent, slot.payloadLen);
                    completeTx(txId, TX_DONE_FAIL);
                }
                return;
            }

            case TX_WAIT_RESPONSE: {
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                while (worker.client.available() && slot.responseLen < sizeof(slot.response)) {
                    int avail = worker.client.available();
                    int space = sizeof(slot.response) - slot.responseLen;
                    int toRead = min(avail, space);
                    int got = worker.client.read(&slot.response[slot.responseLen], toRead);
                    if (got > 0) {
                        slot.responseLen += got;
                        worker.lastActivity = now;
                    } else {
                        break;
                    }
                }
                bool alive = worker.client.connected();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (slot.responseLen >= sizeof(slot.response)) {
                    XTP_TCP_LOGF("[tcp] tx[%d] received %u byte response\n", txId, slot.responseLen);
                    completeTx(txId, TX_DONE_OK);
                    return;
                }

                if (!alive) {
                    if (slot.responseLen > 0) {
                        XTP_TCP_LOGF("[tcp] tx[%d] received %u byte response\n", txId, slot.responseLen);
                        completeTx(txId, TX_DONE_OK);
                    } else {
                        XTP_TCP_LOGF("[tcp] tx[%d] disconnected while waiting\n", txId);
                        completeTx(txId, TX_DONE_FAIL);
                    }
                    return;
                }

                if (slot.responseLen > 0) {
                    if ((now - worker.lastActivity) > slot.responseTimeout) {
                        XTP_TCP_LOGF("[tcp] tx[%d] received %u byte response\n", txId, slot.responseLen);
                        completeTx(txId, TX_DONE_OK);
                    }
                    return;
                }

                if ((now - slot.sendTime) > slot.responseTimeout) {
                    XTP_TCP_LOGF("[tcp] tx[%d] response timeout\n", txId);
                    completeTx(txId, TX_DONE_FAIL);
                }
                return;
            }

            case TX_DONE_OK:
            case TX_DONE_FAIL:
            case TX_CANCELLED:
                stopWorker(workerIndex);
                return;

            default:
                return;
        }
    }

    void processTxQueue() {
        uint32_t now = millis();

        for (uint8_t i = 0; i < MAX_SOCK_NUM; i++) {
            if (_txWorkers[i].txId >= 0) {
                processWorker(i, now);
            }
        }

        while (true) {
            int8_t next = findNextPendingTx();
            int8_t workerIndex = findIdleWorker();
            if (next < 0 || workerIndex < 0) {
                return;
            }

            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
            uint8_t availSockets = countAvailableSockets();
            if (availSockets == 0) {
                tryFreeStuckSocket();
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                return;
            }
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

            startTxOnWorker((uint8_t)workerIndex, next, now);
        }
    }

    bool beginQueuedTx(IPAddress host, uint16_t port) {
        if (_frontTx >= 0) {
            txRelease(_frontTx);
            _frontTx = -1;
        }
        if (_state != TCP_IDLE && !_keepAlive) {
            disconnect("Switching to queued request");
        }

        int8_t id = findFreeSlot();
        if (id < 0) {
            XTP_TCP_LOGLN("[tcp] tx queue full");
            return false;
        }

        TxSlot& slot = _txSlots[id];
        slot.reset();
        slot.state = TX_BUILDING;
        slot.frontend = true;
        slot.host = host;
        slot.port = port;
        slot.startTime = millis();
        slot.timeout = _connectTimeout;
        slot.responseTimeout = _queuedResponseTimeout;
        slot.waitForResponse = _queuedWaitForResponse;
        _frontTx = id;
        return true;
    }

    bool beginQueuedTx(const char* host, uint16_t port) {
        if (_frontTx >= 0) {
            txRelease(_frontTx);
            _frontTx = -1;
        }
        if (_state != TCP_IDLE && !_keepAlive) {
            disconnect("Switching to queued request");
        }

        int8_t id = findFreeSlot();
        if (id < 0) {
            XTP_TCP_LOGLN("[tcp] tx queue full");
            return false;
        }

        TxSlot& slot = _txSlots[id];
        slot.reset();
        slot.state = TX_BUILDING;
        slot.frontend = true;
        strncpy(slot.hostStr, host, sizeof(slot.hostStr) - 1);
        slot.hostStr[sizeof(slot.hostStr) - 1] = '\0';
        slot.useHostStr = !slot.host.fromString(host);
        slot.port = port;
        slot.startTime = millis();
        slot.timeout = _connectTimeout;
        slot.responseTimeout = _queuedResponseTimeout;
        slot.waitForResponse = _queuedWaitForResponse;
        _frontTx = id;
        return true;
    }

    size_t appendToFrontTx(const uint8_t* data, size_t length) {
        if (_frontTx < 0 || !data || length == 0) {
            return 0;
        }

        TxSlot& slot = _txSlots[_frontTx];
        if (slot.state != TX_BUILDING) {
            return 0;
        }

        size_t available = XTP_TCP_TX_BUF_SIZE - slot.payloadLen;
        size_t toCopy = min(length, available);
        if (toCopy == 0) {
            return 0;
        }

        memcpy(slot.payload + slot.payloadLen, data, toCopy);
        slot.payloadLen += (uint16_t)toCopy;
        return toCopy;
    }

    void finalizeFrontTx() {
        if (_frontTx < 0) {
            return;
        }

        TxSlot& slot = _txSlots[_frontTx];
        if (slot.state != TX_BUILDING) {
            _frontTx = -1;
            return;
        }

        slot.state = TX_PENDING;
        if (slot.useHostStr) {
            XTP_TCP_LOGF("[tcp] tx[%d] queued %u bytes to %s:%d%s\n",
                         _frontTx, slot.payloadLen, slot.hostStr, slot.port,
                         slot.waitForResponse ? " (await response)" : "");
        } else {
            XTP_TCP_LOGF("[tcp] tx[%d] queued %u bytes to %d.%d.%d.%d:%d%s\n",
                         _frontTx, slot.payloadLen,
                         slot.host[0], slot.host[1], slot.host[2], slot.host[3], slot.port,
                         slot.waitForResponse ? " (await response)" : "");
        }
        _frontTx = -1;
    }

    void releaseFrontResponseIfConsumed(int8_t id) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return;
        }
        TxSlot& slot = _txSlots[id];
        if (slot.frontend && slot.state == TX_DONE_OK && slot.readOffset >= slot.responseLen) {
            txRelease(id);
        }
    }

public:
    XtpTcpClient() {
        registerSelf();
    }

    ~XtpTcpClient() {
        unregisterSelf();
    }

    XtpTcpClient(const XtpTcpClient&) = delete;
    XtpTcpClient& operator=(const XtpTcpClient&) = delete;
    XtpTcpClient(XtpTcpClient&&) = delete;
    XtpTcpClient& operator=(XtpTcpClient&&) = delete;

    template<typename EthStateT>
    static void serviceAll(EthStateT& ethState) {
        Registry& value = registry();
        for (uint8_t i = 0; i < XTP_TCP_CLIENT_INSTANCE_MAX; i++) {
            if (value.instances[i] != nullptr) {
                value.instances[i]->loop(ethState);
            }
        }
    }

    void setConnectTimeout(uint32_t ms)        { _connectTimeout = ms; }
    void setReconnectInterval(uint32_t ms)     { _reconnectInterval = ms; }
    void setIdleTimeout(uint32_t ms)           { _idleTimeout = ms; }
    void setAutoReconnect(bool enable)         { _autoReconnect = enable; }
    void setKeepAlive(bool enable)             { _keepAlive = enable; }
    void setQueuedWaitForResponse(bool enable) { _queuedWaitForResponse = enable; }
    void setQueuedResponseTimeout(uint32_t ms) { _queuedResponseTimeout = ms; }

    void onConnect(ConnectCallback cb)       { _onConnect = cb; }
    void onDisconnect(DisconnectCallback cb) { _onDisconnect = cb; }
    void onData(DataCallback cb)             { _onData = cb; }

    bool connect(IPAddress host, uint16_t port) {
        if (!_keepAlive) {
            return beginQueuedTx(host, port);
        }

        if (_frontTx >= 0) {
            txRelease(_frontTx);
            _frontTx = -1;
        }
        if (_state == TCP_CONNECTED || _state == TCP_CONNECTING) {
            close();
        }
        _host = host;
        _port = port;
        _useHostStr = false;
        _hasTarget = true;
        _lastAttempt = 0;
        _disconnectTime = 0;
        _state = TCP_IDLE;
        return true;
    }

    bool connect(const char* host, uint16_t port) {
        if (!_keepAlive) {
            return beginQueuedTx(host, port);
        }

        if (_frontTx >= 0) {
            txRelease(_frontTx);
            _frontTx = -1;
        }
        if (_state == TCP_CONNECTED || _state == TCP_CONNECTING) {
            close();
        }
        strncpy(_hostStr, host, sizeof(_hostStr) - 1);
        _hostStr[sizeof(_hostStr) - 1] = '\0';
        _useHostStr = !_host.fromString(host);
        _port = port;
        _hasTarget = true;
        _lastAttempt = 0;
        _disconnectTime = 0;
        _state = TCP_IDLE;
        return true;
    }

    bool connectPersistent(IPAddress host, uint16_t port) {
        _keepAlive = true;
        return connect(host, port);
    }

    bool connectPersistent(const char* host, uint16_t port) {
        _keepAlive = true;
        return connect(host, port);
    }

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

    int8_t send(IPAddress host, uint16_t port, const char* str,
                TxDoneCallback callback = nullptr,
                bool waitForResponse = false,
                uint32_t responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS) {
        return send(host, port, (const uint8_t*)str, (uint16_t)strlen(str),
                    callback, waitForResponse, responseTimeout);
    }

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

    int8_t send(const char* host, uint16_t port, const char* str,
                TxDoneCallback callback = nullptr,
                bool waitForResponse = false,
                uint32_t responseTimeout = XTP_TCP_TX_RESPONSE_TIMEOUT_MS) {
        return send(host, port, (const uint8_t*)str, (uint16_t)strlen(str),
                    callback, waitForResponse, responseTimeout);
    }

    TxState txState(int8_t id) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return TX_FREE;
        }

        TxState state = _txSlots[id].state;
        if (isTerminalState(state)) {
            if (_frontTx == id) {
                _frontTx = -1;
            }
            _txSlots[id].reset();
        }
        return state;
    }

    TxState txPeek(int8_t id) const {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return TX_FREE;
        }
        return _txSlots[id].state;
    }

    uint16_t txResponse(int8_t id, uint8_t* buf, uint16_t maxLen) const {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return 0;
        }

        const TxSlot& slot = _txSlots[id];
        if (slot.state != TX_DONE_OK) {
            return 0;
        }

        uint16_t copyLen = min(slot.responseLen, maxLen);
        if (copyLen > 0 && buf) {
            memcpy(buf, slot.response, copyLen);
        }
        return copyLen;
    }

    void txRelease(int8_t id) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return;
        }

        int8_t workerIndex = findWorkerByTx(id);
        if (workerIndex >= 0) {
            stopWorker((uint8_t)workerIndex);
        }
        if (_frontTx == id) {
            _frontTx = -1;
        }
        _txSlots[id].reset();
    }

    bool txCancel(int8_t id) {
        if (id < 0 || id >= XTP_TCP_TX_QUEUE_SIZE) {
            return false;
        }

        TxSlot& slot = _txSlots[id];
        if (slot.state == TX_FREE || isTerminalState(slot.state)) {
            return false;
        }

        XTP_TCP_LOGF("[tcp] tx[%d] cancelled\n", id);
        TxDoneCallback cb = slot.callback;
        int8_t workerIndex = findWorkerByTx(id);
        if (workerIndex >= 0) {
            stopWorker((uint8_t)workerIndex);
        }
        slot.state = TX_CANCELLED;
        slot.completeTime = millis();
        if (cb) {
            cb(id, TX_CANCELLED, nullptr, 0);
        }
        if (slot.frontend || _frontTx == id) {
            if (_frontTx == id) {
                _frontTx = -1;
            }
            slot.reset();
        }
        return true;
    }

    uint8_t txPending() const {
        uint8_t count = 0;
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            TxState state = _txSlots[i].state;
            if (state == TX_PENDING ||
                state == TX_CONNECTING ||
                state == TX_SENDING ||
                state == TX_WAIT_RESPONSE) {
                count++;
            }
        }
        return count;
    }

    uint8_t txFreeSlots() const {
        uint8_t count = 0;
        for (int8_t i = 0; i < XTP_TCP_TX_QUEUE_SIZE; i++) {
            if (_txSlots[i].state == TX_FREE) {
                count++;
            }
        }
        return count;
    }

    void close() {
        if (!_keepAlive && _frontTx >= 0) {
            txRelease(_frontTx);
            _frontTx = -1;
            return;
        }
        if (_state == TCP_IDLE) {
            return;
        }
        _hasTarget = _autoReconnect && _keepAlive;
        disconnect("Connection closed by user");
    }

    void stop() {
        if (!_keepAlive && _frontTx >= 0) {
            finalizeFrontTx();
            return;
        }

        _hasTarget = false;
        _autoReconnect = false;
        if (_state != TCP_IDLE) {
            disconnect("Stopped");
        }
    }

    bool isConnected() const    { return _state == TCP_CONNECTED; }
    bool isConnecting() const   { return _state == TCP_CONNECTING; }
    bool isIdle() const         { return _state == TCP_IDLE; }
    bool isKeepAlive() const    { return _keepAlive; }
    State getState() const      { return _state; }
    IPAddress remoteIP() const  { return _host; }
    uint16_t remotePort() const { return _port; }
    uint8_t connected() const {
        return (_state == TCP_CONNECTED ||
                _state == TCP_CONNECTING ||
                _frontTx >= 0 ||
                hasFrontendTxInFlight() ||
                findNextReadableFrontendTx() >= 0) ? 1 : 0;
    }
    operator bool() const { return connected() != 0; }

    EthernetClient& raw() { return _client; }

    size_t write(const uint8_t* data, size_t length) {
        if (!_keepAlive && _frontTx >= 0) {
            return appendToFrontTx(data, length);
        }
        if (_state != TCP_CONNECTED) {
            return 0;
        }

        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        size_t written = _client.write(data, length);
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
        if (written > 0) {
            _lastActivity = millis();
        }
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
        size_t count = print(str);
        count += write((const uint8_t*)"\r\n", 2);
        return count;
    }

    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (len <= 0) {
            return 0;
        }
        if (len > (int)sizeof(buf) - 1) {
            len = sizeof(buf) - 1;
        }
        return write((const uint8_t*)buf, (size_t)len);
    }

    void flush() {
        if (!_keepAlive && _frontTx >= 0) {
            return;
        }
        if (_state != TCP_CONNECTED) {
            return;
        }

        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
        _client.flush();
        XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
    }

    int available() {
        if (_state == TCP_CONNECTED) {
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
            int avail = _client.available();
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
            return avail;
        }

        int8_t id = findNextReadableFrontendTx();
        if (id < 0) {
            return 0;
        }
        return _txSlots[id].responseLen - _txSlots[id].readOffset;
    }

    int read() {
        if (_state == TCP_CONNECTED) {
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
            int c = _client.read();
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
            if (c >= 0) {
                _lastActivity = millis();
            }
            return c;
        }

        int8_t id = findNextReadableFrontendTx();
        if (id < 0) {
            return -1;
        }

        TxSlot& slot = _txSlots[id];
        int c = slot.response[slot.readOffset++];
        releaseFrontResponseIfConsumed(id);
        return c;
    }

    int read(uint8_t* buf, size_t size) {
        if (_state == TCP_CONNECTED) {
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
            int got = _client.read(buf, size);
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
            if (got > 0) {
                _lastActivity = millis();
            }
            return got;
        }

        int8_t id = findNextReadableFrontendTx();
        if (id < 0 || !buf || size == 0) {
            return 0;
        }

        TxSlot& slot = _txSlots[id];
        uint16_t remaining = slot.responseLen - slot.readOffset;
        uint16_t toCopy = min<uint16_t>(remaining, (uint16_t)size);
        memcpy(buf, slot.response + slot.readOffset, toCopy);
        slot.readOffset += toCopy;
        releaseFrontResponseIfConsumed(id);
        return toCopy;
    }

    int peek() {
        if (_state == TCP_CONNECTED) {
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
            int c = _client.peek();
            XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
            return c;
        }

        int8_t id = findNextReadableFrontendTx();
        if (id < 0) {
            return -1;
        }
        return _txSlots[id].response[_txSlots[id].readOffset];
    }

    template<typename EthStateT>
    void loop(EthStateT& ethState) {
        if (!ethState.isReady()) {
            if (_state != TCP_IDLE) {
                disconnect("Ethernet not ready");
            }
            failRunningWorkers("Ethernet not ready");
            return;
        }

        if (ethState.initCycle != _ethInitCycle) {
            _ethInitCycle = ethState.initCycle;
            failRunningWorkers("Ethernet reinitialized");
            _client = EthernetClient();
            _state = TCP_IDLE;
            _lastActivity = 0;
            _disconnectTime = 0;
            _lastAttempt = millis() - _reconnectInterval + XTP_TCP_ETH_STABILIZE_MS;
            XTP_TCP_LOGLN("[tcp] Ethernet reinitialized, waiting before connect");
            return;
        }

        uint32_t now = millis();
        processTxQueue();

        bool needConnection = _keepAlive && _hasTarget;

        switch (_state) {
            case TCP_IDLE: {
                if (!needConnection) {
                    return;
                }
                if (_lastAttempt != 0 && (now - _lastAttempt) < _reconnectInterval) {
                    return;
                }
                if (_disconnectTime != 0 && (now - _disconnectTime) < XTP_TCP_SOCKET_RELEASE_MS) {
                    return;
                }

                _lastAttempt = now;
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
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
                bool connectedNow = _useHostStr
                    ? _client.connect(_hostStr, _port)
                    : _client.connect(_host, _port);
                XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);

                if (!connectedNow) {
                    XTP_TCP_LOGLN("[tcp] TCP connect failed");
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    _client.stop();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                    _client = EthernetClient();
                    _disconnectTime = now;
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
                    if (_onConnect) {
                        _onConnect();
                    }
                    return;
                }

                if ((now - _connectStart) > _connectTimeout) {
                    disconnect("Connect timeout");
                    _lastAttempt = now;
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
                    return;
                }

                if (_idleTimeout > 0 && (now - _lastActivity) > _idleTimeout) {
                    disconnect("Idle timeout");
                    _lastAttempt = now;
                    return;
                }

                if (_onData) {
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_ETH);
                    handleIncomingData();
                    XTP_TCP_SPI_SELECT(XTP_TCP_SPI_NONE);
                }
                break;
            }

            case TCP_DISCONNECTING:
                disconnect("Disconnecting");
                break;

            default:
                _state = TCP_IDLE;
                break;
        }
    }

    void loop() {
        struct DummyEthState {
            bool isReady() const { return true; }
            uint8_t initCycle = 0;
        };
        static DummyEthState dummy;
        loop(dummy);
    }
};
