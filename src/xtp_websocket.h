#pragma once

#ifdef XTP_WEBSOCKETS

#include <Arduino.h>
#include <Ethernet.h>
#include <utility/w5100.h>   // W5100 class for direct non-blocking socket control
#include "xtp_config.h"

// ============================================================================
// Debug Logging
// ============================================================================

#ifdef XTP_WS_DEBUG
  #define WS_LOG(...)   Serial.print(__VA_ARGS__)
  #define WS_LOGLN(...) Serial.println(__VA_ARGS__)
#else
  #define WS_LOG(...)
  #define WS_LOGLN(...)
#endif

// ============================================================================
// Configuration & Constants
// ============================================================================

#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS 4
#endif

#ifndef WS_MAX_SUBS
#define WS_MAX_SUBS 4
#endif

#ifndef WS_MAX_PROPS
#define WS_MAX_PROPS 2
#endif

#ifndef WS_KEY_LEN
#define WS_KEY_LEN 12
#endif

#ifndef WS_VAL_LEN
#define WS_VAL_LEN 32
#endif

#ifndef WS_RX_BUFFER_SIZE
#define WS_RX_BUFFER_SIZE 256
#endif

// TX buffer for queued outgoing data (per client)
#ifndef WS_TX_BUFFER_SIZE
#define WS_TX_BUFFER_SIZE 4096
#endif

// Maximum chunk size per write (W5500 safe limit)
#ifndef WS_TX_CHUNK_SIZE
#define WS_TX_CHUNK_SIZE 1024
#endif

// Line buffer for streaming header parsing (only needs to hold one header line)
#ifndef WS_LINE_BUFFER_SIZE
#define WS_LINE_BUFFER_SIZE 128
#endif

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_PING_INTERVAL_MS 10000
#define WS_TIMEOUT_MS 30000

// Max time (ms) to tolerate W5500 TX buffer being full before force-closing
#ifndef WS_TX_STALL_TIMEOUT_MS
#define WS_TX_STALL_TIMEOUT_MS 2000
#endif

// ============================================================================
// Structs
// ============================================================================

struct WsProperty {
    char key[WS_KEY_LEN];
    char value[WS_VAL_LEN];
};

struct WsSubscription {
    char topic[32];
    WsProperty properties[WS_MAX_PROPS];
    uint8_t propCount = 0;
    
    void addProp(const char* k, const char* v) {
        if (propCount < WS_MAX_PROPS) {
            strncpy(properties[propCount].key, k, WS_KEY_LEN - 1);
            properties[propCount].key[WS_KEY_LEN - 1] = 0;
            strncpy(properties[propCount].value, v, WS_VAL_LEN - 1);
            properties[propCount].value[WS_VAL_LEN - 1] = 0;
            propCount++;
        }
    }
    
    void clear() {
        propCount = 0;
        topic[0] = 0;
    }
};

// ============================================================================
// Ring Buffer for TX Queue
// ============================================================================

class WsTxBuffer {
public:
    uint8_t buffer[WS_TX_BUFFER_SIZE];
    volatile uint16_t head = 0;  // Write position
    volatile uint16_t tail = 0;  // Read position
    
    void reset() {
        head = tail = 0;
    }
    
    uint16_t available() const {
        if (head >= tail) return head - tail;
        return WS_TX_BUFFER_SIZE - tail + head;
    }
    
    uint16_t freeSpace() const {
        return WS_TX_BUFFER_SIZE - available() - 1;
    }
    
    bool isEmpty() const {
        return head == tail;
    }
    
    bool write(const uint8_t* data, uint16_t len) {
        if (len > freeSpace()) return false; // Not enough space
        
        for (uint16_t i = 0; i < len; i++) {
            buffer[head] = data[i];
            head = (head + 1) % WS_TX_BUFFER_SIZE;
        }
        return true;
    }
    
    // Read up to 'len' bytes into 'dest', return actual count read
    uint16_t read(uint8_t* dest, uint16_t maxLen) {
        uint16_t count = 0;
        while (count < maxLen && tail != head) {
            dest[count++] = buffer[tail];
            tail = (tail + 1) % WS_TX_BUFFER_SIZE;
        }
        return count;
    }
    
    // Peek at data without removing (for debugging)
    uint8_t peek(uint16_t offset) const {
        return buffer[(tail + offset) % WS_TX_BUFFER_SIZE];
    }
};

// ============================================================================
// Crypto Helpers (SHA1 + Base64)
// ============================================================================

class WsCrypto {
public:
    static String generateAcceptKey(String key) {
        key += WS_GUID;
        uint8_t sha1Hash[20];
        sha1(key, sha1Hash);
        return base64Encode(sha1Hash, 20);
    }

private:
    static void sha1(String data, uint8_t* hash) {
        uint32_t w[80];
        uint32_t a, b, c, d, e, temp;
        uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        
        uint32_t len = data.length();
        uint64_t bit_len = (uint64_t)len * 8;
        uint32_t new_len = len + 1;
        while ((new_len % 64) != 56) new_len++;
        new_len += 8;
        
        uint8_t* msg = (uint8_t*)malloc(new_len);
        if (!msg) return;
        
        memset(msg, 0, new_len);
        memcpy(msg, data.c_str(), len);
        msg[len] = 0x80;
        
        for (int i = 0; i < 8; i++) {
            msg[new_len - 1 - i] = (bit_len >> (i * 8)) & 0xFF;
        }

        for (uint32_t offset = 0; offset < new_len; offset += 64) {
            uint32_t* block = (uint32_t*)(msg + offset);
            
            for (int i = 0; i < 16; i++) {
                uint32_t val = block[i];
                w[i] = (val << 24) | ((val & 0xFF00) << 8) | ((val >> 8) & 0xFF00) | (val >> 24);
            }
            
            for (int i = 16; i < 80; i++) {
                w[i] = rotateLeft(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
            }

            a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];

            for (int i = 0; i < 80; i++) {
                if (i < 20) temp = rotateLeft(a, 5) + ((b & c) | ((~b) & d)) + 0x5A827999 + w[i] + e;
                else if (i < 40) temp = rotateLeft(a, 5) + (b ^ c ^ d) + 0x6ED9EBA1 + w[i] + e;
                else if (i < 60) temp = rotateLeft(a, 5) + ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC + w[i] + e;
                else temp = rotateLeft(a, 5) + (b ^ c ^ d) + 0xCA62C1D6 + w[i] + e;
                
                e = d; d = c; c = rotateLeft(b, 30); b = a; a = temp;
            }
            
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        
        free(msg);
        
        for (int i = 0; i < 20; i++) {
            hash[i] = (h[i >> 2] >> (24 - (i & 3) * 8)) & 0xFF;
        }
    }

    static uint32_t rotateLeft(uint32_t value, unsigned int count) {
        return (value << count) | (value >> (32 - count));
    }

    static String base64Encode(uint8_t* data, uint32_t len) {
        const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String out = "";
        out.reserve((len * 4 / 3) + 4);
        int i = 0, j = 0;
        uint8_t char_array_3[3];
        uint8_t char_array_4[4];

        while (len--) {
            char_array_3[i++] = *(data++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;
                for (i = 0; i < 4; i++) out += b64[char_array_4[i]];
                i = 0;
            }
        }
        if (i) {
            for (j = i; j < 3; j++) char_array_3[j] = '\0';
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (j = 0; j < i + 1; j++) out += b64[char_array_4[j]];
            while (i++ < 3) out += '=';
        }
        return out;
    }
};

// ============================================================================
// WebSocket Client State Machine
// ============================================================================

enum WsState {
    WS_DISCONNECTED = 0,
    WS_HANDSHAKE_RECV,      // Receiving HTTP upgrade request
    WS_HANDSHAKE_SEND,      // Sending HTTP upgrade response
    WS_CONNECTED,           // Normal operation
    WS_CLOSING              // Graceful close in progress
};

enum WsOpcode {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xA
};

class WebSocketClient {
public:
    uint8_t id;
    EthernetClient client;
    WsState state = WS_DISCONNECTED;
    uint32_t lastActive = 0;
    uint32_t lastPing = 0;
    
    WsSubscription subscriptions[WS_MAX_SUBS];
    
    // RX Buffer for frame reassembly (small - only for WS frames after handshake)
    uint8_t rxBuffer[WS_RX_BUFFER_SIZE];
    uint16_t rxIndex = 0;
    
    // Line buffer for streaming handshake parsing
    char lineBuffer[WS_LINE_BUFFER_SIZE];
    uint8_t lineIndex = 0;
    bool sawCR = false;  // Track if we saw \r
    
    // TX Ring Buffer for non-blocking sends
    WsTxBuffer txBuffer;
    
    // Handshake - only store the key (24 bytes base64)
    char wsKey[28];  // Base64 key is 24 chars + null
    
    // TX stall detection: timestamp when W5500 TX buffer first became full (0 = not stalled)
    uint32_t txStallStart = 0;

    WebSocketClient() : id(0) {}

    void init(uint8_t _id, EthernetClient _client) {
        id = _id;
        client = _client;
        state = WS_HANDSHAKE_RECV;
        lastActive = millis();
        lastPing = millis();
        clearSubscriptions();
        rxIndex = 0;
        lineIndex = 0;
        sawCR = false;
        wsKey[0] = 0;
        txBuffer.reset();
        txStallStart = 0;
    }

    void disconnect() {
        if (client.connected()) client.stop();
        state = WS_DISCONNECTED;
        clearSubscriptions();
        txBuffer.reset();
        rxIndex = 0;
        txStallStart = 0;
    }

    // Force-close: non-blocking socket kill for link-down scenarios.
    // client.stop() blocks (tries graceful FIN + waits up to 1s).
    // W5100.execCmdSn(Sock_CLOSE) is a single SPI command — instant.
    void forceClose() {
        uint8_t sock = client.getSocketNumber();
        if (sock < MAX_SOCK_NUM) {
            SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
            W5100.execCmdSn(sock, Sock_CLOSE);
            SPI.endTransaction();
        }
        state = WS_DISCONNECTED;
        clearSubscriptions();
        txBuffer.reset();
        rxIndex = 0;
        txStallStart = 0;
    }

    void clearSubscriptions() {
        for (int i = 0; i < WS_MAX_SUBS; i++) subscriptions[i].clear();
    }
    
    WsSubscription* getEmptySubscription() {
        for (int i = 0; i < WS_MAX_SUBS; i++) {
            if (strlen(subscriptions[i].topic) == 0) return &subscriptions[i];
        }
        return nullptr;
    }

    // Queue a WebSocket frame for transmission (non-blocking)
    bool queueFrame(uint8_t opcode, const void* payload, uint16_t length) {
        if (state != WS_CONNECTED) return false;
        
        // Build frame header
        uint8_t header[4];
        uint8_t headerLen = 0;
        
        header[0] = 0x80 | (opcode & 0x0F); // FIN + Opcode
        
        if (length <= 125) {
            header[1] = (uint8_t)length;
            headerLen = 2;
        } else if (length <= 65535) {
            header[1] = 126;
            header[2] = (length >> 8) & 0xFF;
            header[3] = length & 0xFF;
            headerLen = 4;
        } else {
            return false; // Too large
        }
        
        // Check if we have space for header + payload
        uint16_t totalSize = headerLen + length;
        if (txBuffer.freeSpace() < totalSize) {
            WS_LOG("WS TX Full: need "); WS_LOG(totalSize); 
            WS_LOG(" have "); WS_LOGLN(txBuffer.freeSpace());
            return false; // TX buffer full, drop frame
        }
        
        // Queue header
        txBuffer.write(header, headerLen);
        
        // Queue payload
        if (length > 0) {
            txBuffer.write((const uint8_t*)payload, length);
        }
        
        return true;
    }
    
    bool queueText(const char* text) {
        return queueFrame(WS_OP_TEXT, text, strlen(text));
    }
    
    // ── Non-blocking TX state machine ─────────────────────────
    // Instead of calling client.write() which enters socketSend()'s
    // blocking while-loops, we pre-check W5500 TX buffer space via
    // availableForWrite() (a single non-blocking SPI register read).
    // If no space: return immediately, come back next loop().
    // If stalled for too long: force-close the client.
    // This converts both blocking loops into a check-and-return pattern.
    void processTx() {
        if (txBuffer.isEmpty()) { txStallStart = 0; return; }
        
        // Check socket is still in a writable state
        uint8_t sockStat = client.status();
        if (sockStat != 0x17 /*ESTABLISHED*/ && sockStat != 0x1C /*CLOSE_WAIT*/) {
            WS_LOG("WS TX: socket state 0x"); WS_LOGLN(sockStat);
            txBuffer.reset(); txStallStart = 0;
            return;
        }
        
        // Check W5500 hardware TX buffer space (non-blocking SPI read)
        int hwAvail = client.availableForWrite();
        if (hwAvail <= 0) {
            // W5500 TX buffer full — don't enter socketSend(), just return
            uint32_t now = millis();
            if (txStallStart == 0) {
                txStallStart = now;
                WS_LOG("WS TX stall: W5500 buffer full, client "); WS_LOGLN(id);
            } else if (now - txStallStart > WS_TX_STALL_TIMEOUT_MS) {
                WS_LOG("WS TX stall timeout ("); WS_LOG(WS_TX_STALL_TIMEOUT_MS);
                WS_LOG("ms), force-closing client "); WS_LOGLN(id);
                forceClose();
            }
            return;  // Come back next loop iteration
        }
        txStallStart = 0;  // W5500 has space, not stalled
        
        // Send chunks, clamped to available W5500 TX buffer space.
        // Since we only write what fits, socketSend()'s Phase 1 loop
        // (wait for free space) passes through in one iteration.
        // Phase 2 (wait for SEND_OK) completes in microseconds since
        // the W5500 only needs to accept the data into its TCP pipeline.
        uint8_t chunk[WS_TX_CHUNK_SIZE];
        int maxChunks = 4;
        
        while (maxChunks-- > 0 && !txBuffer.isEmpty()) {
            hwAvail = client.availableForWrite();
            if (hwAvail <= 0) break;  // W5500 buffer filled up mid-drain
            
            // Clamp to min(chunk_size, hw_available, ring_buffer_available)
            uint16_t toSend = txBuffer.available();
            if (toSend > (uint16_t)WS_TX_CHUNK_SIZE) toSend = WS_TX_CHUNK_SIZE;
            if (toSend > (uint16_t)hwAvail) toSend = (uint16_t)hwAvail;
            if (toSend == 0) break;
            
            uint16_t count = txBuffer.read(chunk, toSend);
            if (count > 0) {
                size_t written = client.write(chunk, count);
                if (written < count) {
                    WS_LOG("WS TX partial: "); WS_LOG(written);
                    WS_LOG("/"); WS_LOGLN(count);
                    break;  // Unexpected partial write, retry next loop
                }
            }
        }
    }
    
    // Queue a control frame (PING/PONG) via the ring buffer.
    // Unlike the old sendFrameBlocking, this never calls write/flush directly.
    bool queueControlFrame(uint8_t opcode, const void* payload, uint16_t length) {
        return queueFrame(opcode, payload, length);
    }
};

// ============================================================================
// WebSocket Server
// ============================================================================

typedef void (*WsMessageHandler)(WebSocketClient& client, const char* msg, uint16_t len);

class WebSocketServer {
private:
    EthernetServer* server;
    WebSocketClient clients[WS_MAX_CLIENTS];
    WsMessageHandler onMessageCallback = nullptr;

public:
    WebSocketServer(EthernetServer& srv) : server(&srv) {}

    void setMessageHandler(WsMessageHandler handler) {
        onMessageCallback = handler;
    }

    void begin() {}

    void loop() {
        // ── Fast link-down detection ──────────────────────────────
        // W5500 socketSend() blocks in a tight loop waiting for TX
        // buffer space that never frees when cable is pulled (no ACKs).
        // client.connected() only checks socket state (still ESTABLISHED).
        // Check the PHY register directly — instant and non-blocking.
        if (Ethernet.linkStatus() != LinkON) {
            for (int i = 0; i < WS_MAX_CLIENTS; i++) {
                if (clients[i].state != WS_DISCONNECTED) {
                    WS_LOG("WS: Link down, dropping client "); WS_LOGLN(i);
                    clients[i].forceClose();
                }
            }
            return;  // Skip all client I/O when link is down
        }

        handleNewClients();
        
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            WebSocketClient& c = clients[i];
            if (c.state == WS_DISCONNECTED) continue;
            
            // Check connection
            if (!c.client.connected()) {
                WS_LOG("WS: Client "); WS_LOG(i); WS_LOGLN(" disconnected");
                c.disconnect();
                continue;
            }
            
            // State machine
            switch (c.state) {
                case WS_HANDSHAKE_RECV:
                    processHandshakeRecv(c);
                    break;
                    
                case WS_HANDSHAKE_SEND:
                    processHandshakeSend(c);
                    break;
                    
                case WS_CONNECTED:
                    // RX: Process incoming frames
                    if (c.client.available()) {
                        c.lastActive = millis();
                        processFrame(c);
                    }
                    
                    // TX: Drain TX buffer
                    c.processTx();
                    
                    // Keep-alive
                    checkKeepalive(c);
                    break;
                    
                case WS_CLOSING:
                    c.disconnect();
                    break;
                    
                default:
                    break;
            }
        }
    }

    // Broadcast to topic subscribers
    template <typename Filter>
    void broadcast(const char* msg, Filter filterFunc) {
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (clients[i].state == WS_CONNECTED) {
                if (filterFunc(clients[i])) {
                    clients[i].queueText(msg);
                }
            }
        }
    }

    template <typename Filter>
    void broadcastBinary(const void* data, uint16_t len, Filter filterFunc) {
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (clients[i].state == WS_CONNECTED) {
                if (filterFunc(clients[i])) {
                    clients[i].queueFrame(WS_OP_BINARY, data, len);
                }
            }
        }
    }
    
    void emit(const char* topic, const char* msg) {
        broadcast(msg, [topic](WebSocketClient& c) {
            for (int s = 0; s < WS_MAX_SUBS; s++) {
                if (strcmp(c.subscriptions[s].topic, topic) == 0) return true;
            }
            return false;
        });
    }

    void emitBinary(const char* topic, const void* data, uint16_t len) {
        broadcastBinary(data, len, [topic](WebSocketClient& c) {
            for (int s = 0; s < WS_MAX_SUBS; s++) {
                if (strcmp(c.subscriptions[s].topic, topic) == 0) {
                    return true;
                }
            }
            return false;
        });
    }

    void emitWithProps(const char* topic, const char* msg, const WsProperty* evtProps, uint8_t evtPropCount) {
        broadcast(msg, [&](WebSocketClient& c) {
            for (int s = 0; s < WS_MAX_SUBS; s++) {
                if (strcmp(c.subscriptions[s].topic, topic) == 0) {
                    bool match = true;
                    for (int p = 0; p < c.subscriptions[s].propCount; p++) {
                        const char* reqKey = c.subscriptions[s].properties[p].key;
                        const char* reqVal = c.subscriptions[s].properties[p].value;
                        bool keyFound = false;
                        for (int ep = 0; ep < evtPropCount; ep++) {
                            if (strcmp(evtProps[ep].key, reqKey) == 0) {
                                if (strcmp(evtProps[ep].value, reqVal) != 0) match = false;
                                keyFound = true;
                                break;
                            }
                        }
                        if (!keyFound) match = false;
                        if (!match) break;
                    }
                    if (match) return true;
                }
            }
            return false;
        });
    }

private:
    void handleNewClients() {
        EthernetClient newClient = server->available();
        if (!newClient) return;
        
        // Check if already tracked
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (clients[i].state != WS_DISCONNECTED && clients[i].client == newClient) {
                return; // Already handled
            }
        }
        
        WS_LOGLN("WS: New connection");
        
        if (newClient.connected()) {
            int freeSlot = -1;
            for (int i = 0; i < WS_MAX_CLIENTS; i++) {
                if (clients[i].state == WS_DISCONNECTED || !clients[i].client.connected()) {
                    freeSlot = i;
                    break;
                }
            }
            
            if (freeSlot != -1) {
                WS_LOG("WS: Accepted slot "); WS_LOGLN(freeSlot);
                clients[freeSlot].init(freeSlot, newClient);
            } else {
                WS_LOGLN("WS: Server full");
                newClient.stop();
            }
        }
    }

    void processHandshakeRecv(WebSocketClient& c) {
        // Stream-parse HTTP headers one byte at a time
        // Only extract Sec-WebSocket-Key, discard everything else
        while (c.client.available()) {
            char ch = c.client.read();
            
            if (ch == '\r') {
                c.sawCR = true;
                continue;
            }
            
            if (ch == '\n') {
                // End of line
                c.lineBuffer[c.lineIndex] = 0;
                
                if (c.lineIndex == 0 && c.sawCR) {
                    // Empty line = end of headers
                    if (c.wsKey[0] == 0) {
                        WS_LOGLN("WS: No key found");
                        c.disconnect();
                        return;
                    }
                    
                    // Send response immediately
                    String acceptKey = WsCrypto::generateAcceptKey(String(c.wsKey));
                    c.client.print(F("HTTP/1.1 101 Switching Protocols\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Sec-WebSocket-Accept: "));
                    c.client.print(acceptKey);
                    c.client.print(F("\r\n\r\n"));
                    // Note: no flush() — W5500 handles TCP transmission
                    // autonomously. flush() blocks until all data is ACKed.
                    
                    c.state = WS_CONNECTED;
                    c.lastActive = millis();
                    c.lastPing = millis();
                    WS_LOGLN("WS: Connected!");
                    return;
                }
                
                // Check if this line is Sec-WebSocket-Key
                if (c.lineIndex > 18) {
                    // Case-insensitive prefix check
                    if ((strncmp(c.lineBuffer, "Sec-WebSocket-Key:", 18) == 0) ||
                        (strncmp(c.lineBuffer, "sec-websocket-key:", 18) == 0)) {
                        // Extract the key value
                        char* val = c.lineBuffer + 18;
                        while (*val == ' ') val++;  // Skip spaces
                        
                        // Copy key (trim trailing spaces)
                        int len = strlen(val);
                        while (len > 0 && val[len-1] == ' ') len--;
                        if (len > 0 && len < 28) {
                            memcpy(c.wsKey, val, len);
                            c.wsKey[len] = 0;
                            WS_LOG("WS Key: "); WS_LOGLN(c.wsKey);
                        }
                    }
                }
                
                // Reset for next line
                c.lineIndex = 0;
                c.sawCR = false;
                continue;
            }
            
            // Regular character - add to line buffer if space
            if (c.lineIndex < WS_LINE_BUFFER_SIZE - 1) {
                c.lineBuffer[c.lineIndex++] = ch;
            }
            // If line is too long, we just truncate it (we only care about the key prefix)
            c.sawCR = false;
        }
    }

    // Handshake send is now done immediately in processHandshakeRecv
    // This state should not be reached, but handle gracefully
    void processHandshakeSend(WebSocketClient& c) {
        c.state = WS_CONNECTED;
        WS_LOGLN("WS: Connected (from send state)");
    }

    void processFrame(WebSocketClient& c) {
        // Read available data
        int avail = c.client.available();
        int readCount = 0;
        while (c.client.available() && c.rxIndex < WS_RX_BUFFER_SIZE) {
            c.rxBuffer[c.rxIndex++] = c.client.read();
            readCount++;
        }

        // Process complete frames
        while (c.rxIndex >= 2) {
            uint8_t b1 = c.rxBuffer[0];
            uint8_t b2 = c.rxBuffer[1];
            
            uint8_t opcode = b1 & 0x0F;
            bool masked = b2 & 0x80;
            uint64_t payloadLen = b2 & 0x7F;
            
            int headerLen = 2;
            if (payloadLen == 126) headerLen += 2;
            else if (payloadLen == 127) headerLen += 8;
            if (masked) headerLen += 4;
            
            // Need full header
            if (c.rxIndex < headerLen) break;
            
            // Parse extended length
            if (payloadLen == 126) {
                payloadLen = ((uint16_t)c.rxBuffer[2] << 8) | c.rxBuffer[3];
            } else if (payloadLen == 127) {
                // 64-bit frames not supported
                c.disconnect();
                return;
            }
            
            uint32_t totalFrameSize = headerLen + (uint32_t)payloadLen;
            
            // Frame too large?
            if (totalFrameSize > WS_RX_BUFFER_SIZE) {
                WS_LOG("WS: Frame too large: "); WS_LOGLN(totalFrameSize);
                c.disconnect();
                return;
            }
            
            // Need full frame
            if (c.rxIndex < totalFrameSize) break;
            
            // Extract and unmask payload
            uint8_t maskKey[4] = {0};
            if (masked) {
                int maskOffset = headerLen - 4;
                for (int k = 0; k < 4; k++) maskKey[k] = c.rxBuffer[maskOffset + k];
            }
            
            char* payloadPtr = (char*)&c.rxBuffer[headerLen];
            if (masked) {
                for (uint32_t i = 0; i < payloadLen; i++) {
                    payloadPtr[i] ^= maskKey[i % 4];
                }
            }
            
            // Handle opcode
            handleOpcode(c, opcode, payloadPtr, (uint16_t)payloadLen);
            
            // Remove processed frame
            int remaining = c.rxIndex - totalFrameSize;
            if (remaining > 0) {
                memmove(c.rxBuffer, &c.rxBuffer[totalFrameSize], remaining);
            }
            c.rxIndex = remaining;
        }
    }

    void handleOpcode(WebSocketClient& c, uint8_t opcode, char* data, uint16_t len) {
        switch (opcode) {
            case WS_OP_TEXT:
                data[len] = 0; // Null terminate (safe due to buffer design)
                if (onMessageCallback) onMessageCallback(c, data, len);
                handleInternalCommands(c, data);
                break;
                
            case WS_OP_BINARY:
                // Binary frames from client not handled in this impl
                break;
                
            case WS_OP_PING:
                c.queueControlFrame(WS_OP_PONG, data, len);
                break;
                
            case WS_OP_CLOSE:
                c.state = WS_CLOSING;
                break;
                
            case WS_OP_PONG:
                c.lastActive = millis();
                WS_LOGLN("WS: PONG");
                break;
        }
    }
    
    void handleInternalCommands(WebSocketClient& c, char* json) {
        // Handled via message callback
    }

    void checkKeepalive(WebSocketClient& c) {
        uint32_t now = millis();
        
        // Send ping (queued, non-blocking)
        if (now - c.lastPing > WS_PING_INTERVAL_MS) {
            c.queueControlFrame(WS_OP_PING, NULL, 0);
            c.lastPing = now;
        }
        
        // Timeout
        if (now - c.lastActive > WS_TIMEOUT_MS) {
            WS_LOG("WS: Timeout client "); WS_LOGLN(c.id);
            c.forceClose();  // Use forceClose instead of blocking disconnect
        }
    }
};

// ============================================================================
// Global Instances
// ============================================================================

EthernetServer wsEthServer(81);
WebSocketServer wsServer(wsEthServer);

void xtp_ws_default_handler(WebSocketClient& c, const char* msg, uint16_t len) {
    WS_LOG("WS Msg: "); WS_LOGLN(msg);

    if (strstr(msg, "\"action\"") && strstr(msg, "\"sub\"")) {
        char* topicStart = strstr((char*)msg, "\"topic\"");
        if (topicStart) {
            char* valStart = strchr(topicStart, ':');
            if (valStart) {
                while (*valStart == ' ' || *valStart == ':' || *valStart == '"') valStart++;
                
                char topic[32] = {0};
                int i = 0;
                while (valStart[i] != '"' && valStart[i] != 0 && valStart[i] != '}' && valStart[i] != ',' && i < 31) {
                    topic[i] = valStart[i];
                    i++;
                }
                
                WS_LOG("WS Sub topic: '"); WS_LOG(topic); WS_LOGLN("'");

                WsSubscription* s = c.getEmptySubscription();
                if (s) {
                    strncpy(s->topic, topic, 31);
                    WS_LOG("  -> Subscribed OK, slot found"); WS_LOGLN("");
                } else {
                    WS_LOGLN("  -> ERROR: No empty subscription slot!");
                }
            }
        }
    }
}

void xtp_ws_setup() {
    wsEthServer.begin();
    wsServer.begin();
    wsServer.setMessageHandler(xtp_ws_default_handler);
}

void xtp_ws_loop() {
    wsServer.loop();
}

#endif // XTP_WEBSOCKETS

