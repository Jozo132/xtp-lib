#pragma once

#ifdef XTP_WEBSOCKETS

#include <Arduino.h>
#include <Ethernet.h>
#include <vector>
#include "xtp_config.h"

// ============================================================================
// Configuration & Constants
// ============================================================================

#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS 4
#endif

#ifndef WS_MAX_SUBS
#define WS_MAX_SUBS 16
#endif

#ifndef WS_MAX_PROPS
#define WS_MAX_PROPS 16
#endif

#ifndef WS_KEY_LEN
#define WS_KEY_LEN 12
#endif

#ifndef WS_VAL_LEN
#define WS_VAL_LEN 32
#endif

#ifndef WS_RX_BUFFER_SIZE
#define WS_RX_BUFFER_SIZE 2048
#endif

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_PING_INTERVAL_MS 10000
#define WS_TIMEOUT_MS 30000

// Structs for properties and subscriptions
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
        uint32_t new_len = len + 1; // data + 0x80
        while ((new_len % 64) != 56) new_len++; // padding 0s
        new_len += 8; // + 64-bit length
        
        uint8_t* msg = (uint8_t*)malloc(new_len);
        if (!msg) return;
        
        memset(msg, 0, new_len);
        memcpy(msg, data.c_str(), len);
        msg[len] = 0x80;
        
        // Append 64-bit length (big endian)
        for (int i = 0; i < 8; i++) {
            msg[new_len - 1 - i] = (bit_len >> (i * 8)) & 0xFF;
        }

        for (uint32_t offset = 0; offset < new_len; offset += 64) {
            uint32_t* block = (uint32_t*)(msg + offset);
            
            for (int i = 0; i < 16; i++) {
                uint32_t val = block[i];
                // Swap endianness
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
// WebSocket Client
// ============================================================================

enum WsState {
    WS_DISCONNECTED,
    WS_HANDSHAKE,
    WS_CONNECTED
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
    
    // RX Buffer for frame reassembly
    uint8_t rxBuffer[WS_RX_BUFFER_SIZE];
    uint16_t rxIndex = 0;
    
    // Handshake storage
    String handshakeKey = "";

    WebSocketClient() : id(0) {}

    void init(uint8_t _id, EthernetClient _client) {
        id = _id;
        client = _client;
        state = WS_HANDSHAKE;
        lastActive = millis();
        lastPing = millis();
        clearSubscriptions();
        rxIndex = 0;
        handshakeKey = "";
    }

    void disconnect() {
        if (client.connected()) client.stop();
        state = WS_DISCONNECTED;
        clearSubscriptions();
    }

    void clearSubscriptions() {
        for (int i = 0; i < WS_MAX_SUBS; i++) subscriptions[i].clear();
    }
    
    // Add sub helper
    WsSubscription* getEmptySubscription() {
        for (int i = 0; i < WS_MAX_SUBS; i++) {
            if (strlen(subscriptions[i].topic) == 0) return &subscriptions[i];
        }
        return nullptr;
    }

    // Send a frame
    void sendFrame(uint8_t opcode, const void* payload, uint16_t length) {
        if (!client.connected()) return;
        
        uint8_t header[4];
        uint8_t headerLen = 0;
        
        header[0] = 0x80 | (opcode & 0x0F); // FIN + Opcode at [0]
        
        if (length <= 125) {
            header[1] = (uint8_t)length; // Mask = 0
            headerLen = 2;
        } else if (length <= 65535) {
            header[1] = 126; // Mask = 0
            header[2] = (length >> 8) & 0xFF;
            header[3] = length & 0xFF;
            headerLen = 4;
        } else {
            // Jumbo frames not supported in this simple impl
            return; 
        }
        
        // Single write call preferred for W5500 packet fragmentation avoidance
        client.write(header, headerLen);
        if (length > 0) client.write((const uint8_t*)payload, length);
        client.flush(); // Force send ensuring PINGs aren't buffered forever
    }
    
    void sendText(const char* text) {
        sendFrame(WS_OP_TEXT, text, strlen(text));
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

    void begin() {
        // Assume server.begin() called externally if shared
        // server->begin(); 
    }

    void loop() {
        handleNewClients();
        handleClientData();
        checkTimeouts();
    }

    // Emit to ALL clients wrapped with filter logic
    // filterFunc returns true if client should receive the message
    // filterFunc signature: bool(WebSocketClient& c)
    template <typename Filter>
    void broadcast(const char* msg, Filter filterFunc) {
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (clients[i].state == WS_CONNECTED && clients[i].client.connected()) {
                if (filterFunc(clients[i])) {
                    clients[i].sendText(msg);
                }
            }
        }
    }
    
    // Broadcast to topic subscribers
    void emit(const char* topic, const char* msg) {
        broadcast(msg, [topic](WebSocketClient& c) {
            for(int s=0; s<WS_MAX_SUBS; s++) {
                // Initial very simple check: topic match
                // User can implement more complex property matching in custom broadcast
                if (strcmp(c.subscriptions[s].topic, topic) == 0) return true;
            }
            return false;
        });
    }

    // Custom emit with property checking
    // properties: array of WsProperty to match against user subscription
    void emitWithProps(const char* topic, const char* msg, const WsProperty* evtProps, uint8_t evtPropCount) {
        broadcast(msg, [&](WebSocketClient& c) {
            for(int s=0; s<WS_MAX_SUBS; s++) {
                if (strcmp(c.subscriptions[s].topic, topic) == 0) {
                    // Topic matched, now check if EVENT properties satisfy SUBSCRIPTION filter
                    // Logic: Subscription Props is a FILTER. Event must match ALL filter props provided by user.
                    // (Or other way around depending on req? Assuming Subs Params are requirements)
                    
                    bool match = true;
                    // For each property defined in the CLIENT'S subscription
                    for(int p=0; p < c.subscriptions[s].propCount; p++) {
                        const char* reqKey = c.subscriptions[s].properties[p].key;
                        const char* reqVal = c.subscriptions[s].properties[p].value;
                        
                        // Find this key in the EVENT properties
                        bool keyFound = false;
                        for(int ep=0; ep < evtPropCount; ep++) {
                            if(strcmp(evtProps[ep].key, reqKey) == 0) {
                                // Simple string match
                                if(strcmp(evtProps[ep].value, reqVal) != 0) {
                                    match = false; // Value mismatch
                                }
                                keyFound = true;
                                break;
                            }
                        }
                        if (!keyFound) match = false; // Required key missing in event
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
        if (newClient) {
            // Check if this socket is already handled by an existing WebSocketClient
            for (int i = 0; i < WS_MAX_CLIENTS; i++) {
                if (clients[i].state != WS_DISCONNECTED && clients[i].client == newClient) {
                    // This is an existing connection with data available, not a new connection
                    return;
                }
            }

             Serial.println("WS: New Connection Attempt");
            if (newClient.connected()) {
                int freeSlot = -1;
                for (int i = 0; i < WS_MAX_CLIENTS; i++) {
                    if (clients[i].state == WS_DISCONNECTED || !clients[i].client.connected()) {
                        freeSlot = i;
                        break;
                    }
                }

                if (freeSlot != -1) {
                    Serial.print("WS: Accepted in slot "); Serial.println(freeSlot);
                    clients[freeSlot].init(freeSlot, newClient);
                } else {
                    Serial.println("WS: Server Full");
                    newClient.stop();
                }
            } else {
                 Serial.println("WS: New conn but not connected?");
            }
        }
    }

    void handleClientData() {
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            WebSocketClient& c = clients[i];
            
            // Check connectivity
            if (c.state != WS_DISCONNECTED) {
                if (!c.client.connected()) {
                     Serial.print("WS: Client "); Serial.print(i); Serial.println(" discon (socket closed)");
                     c.disconnect();
                     continue;
                }
            } else {
                continue; // Skip disconnected slots
            }

            // checkTimeouts() logic moved here for simplicity loop
            if (c.state == WS_CONNECTED) {
                 // Moved back to checkTimeouts to ensure PINGs are sent
            }

            // Only read if data available
            if (c.client.available()) {
                c.lastActive = millis();
                
                if (c.state == WS_HANDSHAKE) {
                    processHandshake(c);
                } else if (c.state == WS_CONNECTED) {
                    processFrame(c);
                }
            }
        }
    }

    void checkTimeouts() {
        uint32_t now = millis();
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            WebSocketClient& c = clients[i];
            if (c.state == WS_CONNECTED) {
                // Keep-alive ping
                if (now - c.lastPing > WS_PING_INTERVAL_MS) {
                    Serial.print("WS: Sending PING to "); Serial.println(i);
                    c.sendFrame(WS_OP_PING, NULL, 0);
                    c.lastPing = now;
                }
                
                // Timeout check moved from handleClientData to here for consistency
                if (now - c.lastActive > WS_TIMEOUT_MS) {
                    Serial.print("WS: Client "); Serial.print(i); Serial.println(" timeout (no PONG)");
                    c.disconnect();
                }
            }
        }
    }

    void processHandshake(WebSocketClient& c) {
        // Read available data into buffer (non-blocking)
        int initialIndex = c.rxIndex;
        while (c.client.available() && c.rxIndex < WS_RX_BUFFER_SIZE - 1) {
            c.rxBuffer[c.rxIndex++] = c.client.read();
        }
        c.rxBuffer[c.rxIndex] = 0; // Null terminate for safety

        if (c.rxIndex > initialIndex) {
            // Serial.print("WS RX: "); Serial.print(c.rxIndex); Serial.println(" bytes accumulated");
        }

        // Process lines from buffer
        int scanPos = 0;
        bool keepScanning = true;
        
        while (keepScanning) {
            // Find newline
            int newlinePos = -1;
            for (int i = scanPos; i < c.rxIndex; i++) {
                if (c.rxBuffer[i] == '\n') {
                    newlinePos = i;
                    break;
                }
            }

            if (newlinePos != -1) {
                // We have a complete line
                // Extract line (handling \r if present)
                int lineEnd = newlinePos;
                if (lineEnd > scanPos && c.rxBuffer[lineEnd - 1] == '\r') {
                    lineEnd--;
                }
                
                // Temporary null terminate to treat as string
                char savedChar = c.rxBuffer[lineEnd];
                c.rxBuffer[lineEnd] = 0;
                String line = String((char*)&c.rxBuffer[scanPos]);
                c.rxBuffer[lineEnd] = savedChar; // Restore
                
                line.trim();
                
                Serial.print("HS Line: "); Serial.println(line);

                if (line.length() == 0) {
                    // Empty line found -> End of Headers
                    if (c.handshakeKey.length() > 0) {
                        String acceptKey = WsCrypto::generateAcceptKey(c.handshakeKey);
                         Serial.print("WS Accept: "); Serial.println(acceptKey);
                        
                        String response = "HTTP/1.1 101 Switching Protocols\r\n"
                                          "Upgrade: websocket\r\n"
                                          "Connection: Upgrade\r\n"
                                          "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
                                          "\r\n";
                        
                        c.client.print(response);
                        c.client.flush();
                        c.state = WS_CONNECTED;
                         Serial.println("WS: Handshake Complete");
                        
                        // Preserve any frame data that arrived with the handshake
                        int remaining = c.rxIndex - (newlinePos + 1);
                        if (remaining > 0) {
                            memmove(c.rxBuffer, &c.rxBuffer[newlinePos + 1], remaining);
                            c.rxIndex = remaining;
                        } else {
                            c.rxIndex = 0; 
                        }
                        return;
                    } else {
                        // Headers ended but no Key?
                        Serial.println("WS Error: Headers ended but no Key found");
                        c.disconnect();
                        return;
                    }
                }
                
                String lower = line;
                lower.toLowerCase();
                if (lower.startsWith("sec-websocket-key:")) {
                    c.handshakeKey = line.substring(18);
                    c.handshakeKey.trim();
                     Serial.print("WS Key: "); Serial.println(c.handshakeKey);
                }

                // Advance scanPos
                scanPos = newlinePos + 1;
            } else {
                // No more newlines in buffer
                keepScanning = false;
            }
        }

        // Shift remaining data to start of buffer
        if (scanPos > 0) {
            int remaining = c.rxIndex - scanPos;
            if (remaining > 0) {
                memmove(c.rxBuffer, &c.rxBuffer[scanPos], remaining);
            }
            c.rxIndex = remaining;
        }

        // Safety: Prevent buffer overflow if line is too long
        if (c.rxIndex >= WS_RX_BUFFER_SIZE - 1) {
             Serial.println("WS Error: Header buffer overflow");
             c.rxIndex = 0; // Discard garbage
             c.disconnect();
        }
    }

    void processFrame(WebSocketClient& c) {
        // 1. Read all available data into buffer
        while (c.client.available() && c.rxIndex < WS_RX_BUFFER_SIZE) {
            c.rxBuffer[c.rxIndex++] = c.client.read();
        }

        // 2. Loop to process frames from buffer
        bool keepProcessing = true;
        while (keepProcessing && c.rxIndex >= 2) {
            // Need at least 2 bytes to check header
            uint8_t b1 = c.rxBuffer[0];
            uint8_t b2 = c.rxBuffer[1];
            
            bool fin = b1 & 0x80;
            uint8_t opcode = b1 & 0x0F;
            bool masked = b2 & 0x80;
            uint64_t payloadLen = b2 & 0x7F;
            
            int headerLen = 2;
            if (payloadLen == 126) headerLen += 2;
            else if (payloadLen == 127) headerLen += 8;
            
            if (masked) headerLen += 4;
            
            // Check if we have the full header
            if (c.rxIndex < headerLen) {
                keepProcessing = false;
                break;
            }
            
            // If header is present, we can determine total frame size
            // (Need to read extended length if applicable)
            if (payloadLen == 126) {
                payloadLen = (c.rxBuffer[2] << 8) | c.rxBuffer[3];
            } else if (payloadLen == 127) {
                 // Ignore/Skip jumbo frames for stability
                 // We don't support 64-bit length properly in this buffer
                 // Strategy: Consume buffer and hope to resync or disconnect
                 c.rxIndex = 0; 
                 c.disconnect(); 
                 return;
            }
            
            uint32_t totalFrameSize = headerLen + (uint32_t)payloadLen;
            
            if (totalFrameSize > WS_RX_BUFFER_SIZE) {
                // Frame too large for buffer
                c.disconnect();
                return;
            }
            
            // Check if we have the full frame
            if (c.rxIndex >= totalFrameSize) {
                // We have a full frame!
                
                // Extract Mask Key
                uint8_t maskKey[4] = {0,0,0,0};
                int maskOffset = headerLen - 4; // default assumption (payloadLen < 126)
                if (payloadLen == 126) maskOffset = 4;
                else if (payloadLen == 127) maskOffset = 10;
                
                if (!masked) maskOffset = 0; // Invalid for client->server frames usually, but handled
                
                if (masked) {
                    for(int k=0; k<4; k++) maskKey[k] = c.rxBuffer[headerLen - 4 + k];
                }
                
                // Unmask and Extract Payload
                // To save memory, we unmask in-place at the frame location, 
                // or just copy strictly the payload to a temp pointer if we were using a separate buffer.
                // Here we can point to the payload in the buffer.
                
                char* payloadPtr = (char*)&c.rxBuffer[headerLen];
                
                if (masked) {
                    for (uint32_t i = 0; i < payloadLen; i++) {
                        payloadPtr[i] ^= maskKey[i % 4];
                    }
                }
                
                // Temporarily null terminate the payload for string handlers
                // WARNING: This overwrites the next byte in the buffer (start of next frame)
                // We must save it, duplicate logic, or shift first.
                // Because we are memmoving later, let's just handle it carefully.
                
                // Safe approach: Handle opcode immediately then shift.
                // We rely on handleOpcode not expecting a null-terminated string unless we guarantee it.
                // Let's guarantee it by being careful. 
                // If payloadLen < 0, nothing to do.
                
                // If `totalFrameSize == c.rxIndex`, the byte at `rxIndex` is invalid. 
                // We can write a null there if `rxIndex < WS_RX_BUFFER_SIZE`.
                
                bool savedByteExists = false;
                uint8_t savedByte = 0;
                
                // If the frame ends exactly at the end of valid data, we can just append 0 if space
                // If the frame ends before the end of data, we save the next byte.
                
                if (c.rxIndex > totalFrameSize) {
                    savedByte = c.rxBuffer[totalFrameSize];
                    savedByteExists = true;
                    c.rxBuffer[totalFrameSize] = 0;
                } else if (totalFrameSize < WS_RX_BUFFER_SIZE) {
                    c.rxBuffer[totalFrameSize] = 0;
                }
                
                handleOpcode(c, opcode, payloadPtr, (uint16_t)payloadLen);
                
                if (savedByteExists) {
                    c.rxBuffer[totalFrameSize] = savedByte;
                }

                // Remove processed frame from buffer
                int remaining = c.rxIndex - totalFrameSize;
                if (remaining > 0) {
                    memmove(c.rxBuffer, &c.rxBuffer[totalFrameSize], remaining);
                }
                c.rxIndex = remaining;
                
            } else {
                // Not enough data for full payload yet
                keepProcessing = false;
            }
        }
    }

    void handleOpcode(WebSocketClient& c, uint8_t opcode, char* data, uint16_t len) {
        switch (opcode) {
            case WS_OP_TEXT:
                // Debug received text
                // Serial.print("WS RX Text: "); Serial.println(data);
                if (onMessageCallback) onMessageCallback(c, data, len);
                handleInternalCommands(c, data);
                break;
            case WS_OP_PING:
                c.sendFrame(WS_OP_PONG, data, len);
                break;
            case WS_OP_CLOSE:
                c.disconnect();
                break;
            case WS_OP_PONG:
                Serial.println("WS: RX PONG");
                c.lastActive = millis();
                break;
        }
    }
    
    // Internal JSON command parser for subscriptions
    // Format: {"action":"sub", "topic":"foo", "params":[{"key":"k","value":"v"}]}
    // Very simplified parser to avoid heavy JSON lib dependency here if possible, 
    // or assume ArduinoJson is available (it is in context).
    void handleInternalCommands(WebSocketClient& c, char* json) {
        // Internal command handling is now done via default message handler
    }
};

EthernetServer wsEthServer(81);
WebSocketServer wsServer(wsEthServer);

void xtp_ws_default_handler(WebSocketClient& c, const char* msg, uint16_t len) {
    Serial.print("WS Msg: "); Serial.println(msg);

    // Check for "action":"sub"
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
                
                Serial.print("WS Sub: "); Serial.println(topic);

                WsSubscription* s = c.getEmptySubscription();
                if (s) {
                    strncpy(s->topic, topic, 31);
                } else {
                    Serial.println("WS Sub Fail: Full");
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

