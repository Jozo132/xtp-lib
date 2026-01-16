#pragma once


#include <Ethernet.h>
#include <utility/w5100.h>
#include "xtp_timing.h"

#define HTTP_MAX_ARGS 32
#define HTTP_MAX_ENDPOINTS 32
#define HTTP_MAX_REMAPS 32
#define HTTP_MAX_BODY_SIZE 1024

#ifndef HTTP_RES_CHUNK_SIZE
#define HTTP_RES_CHUNK_SIZE 2048
#endif /* HTTP_RES_CHUNK_SIZE */

// Socket timeout for stuck connections (ms)
#ifndef HTTP_CLIENT_TIMEOUT_MS
#define HTTP_CLIENT_TIMEOUT_MS 500
#endif

// Interval to check for stuck sockets (ms)
#ifndef HTTP_SOCKET_CLEANUP_INTERVAL_MS
#define HTTP_SOCKET_CLEANUP_INTERVAL_MS 5000
#endif

// Maximum time a socket can be in transitional state before forced cleanup (ms)
#ifndef HTTP_SOCKET_STALE_TIMEOUT_MS
#define HTTP_SOCKET_STALE_TIMEOUT_MS 10000
#endif

// Cached socket status for efficient monitoring (updated periodically)
static uint8_t _cached_socket_status[8] = {0};
static uint16_t _cached_socket_port[8] = {0};
static uint32_t _last_socket_cache_update = 0;

// Socket cache update interval in milliseconds - tune for performance vs accuracy
#ifndef HTTP_SOCKET_CACHE_INTERVAL_MS
#define HTTP_SOCKET_CACHE_INTERVAL_MS 50
#endif

// Update cached socket status (call periodically to avoid SPI overhead)
inline void updateSocketStatusCache() {
    uint32_t now = millis();
    if (now - _last_socket_cache_update < HTTP_SOCKET_CACHE_INTERVAL_MS) return;
    
    XTP_TIMING_START(XTP_TIME_SOCKET_CACHE);
    _last_socket_cache_update = now;
    
    // Batch read all 8 sockets in one go
    for (uint8_t sock = 0; sock < 8; sock++) {
        _cached_socket_status[sock] = W5100.readSnSR(sock);
        _cached_socket_port[sock] = W5100.readSnPORT(sock);
    }
    XTP_TIMING_END(XTP_TIME_SOCKET_CACHE);
}

// Get cached socket status
inline uint8_t cyclic_sock_status(uint8_t sock) {
    updateSocketStatusCache();
    return (sock < 8) ? _cached_socket_status[sock] : 0;
}

// Get cached socket port
inline uint16_t cyclic_sock_port(uint8_t sock) {
    updateSocketStatusCache();
    return (sock < 8) ? _cached_socket_port[sock] : 0;
}

enum HTTPMethod { HTTP_GET, HTTP_POST };

// DIY implementation of a REST server
class RestServer {
public:
    // State machine states - declared first so all methods can use them
    enum State { 
        WAITING,           // Waiting for new client connection
        RECEIVING,         // Receiving request headers and body
        PROCESSING,        // Matching endpoint
        HANDLING,          // Executing handler
        FAILED,            // No matching endpoint found
        CLOSING,           // Gracefully closing connection
        FORCE_CLOSING      // Force closing stuck connection
    };
    
    EthernetServer* server;
    uint32_t _requests_success = 0;
    uint32_t _requests_failed = 0;
    uint32_t _transmitted_bytes = 0;
    char _uri[64];
    IPAddress _ip;
    HTTPMethod _method;
    int _argc;
    struct Argument {
        char name[64];
        char value[64];
    } _args[HTTP_MAX_ARGS];

    char body[HTTP_MAX_BODY_SIZE];
    int body_length = 0;


    typedef void (*EndpointHandler)(void);

    EndpointHandler _notFoundHandler;
    bool _notFoundHandler_defined = false;

    struct Endpoint {
        const char* uri;
        HTTPMethod method;
        EndpointHandler handler;
    };

    Endpoint _endpoints[HTTP_MAX_ENDPOINTS]; // max endpoints
    int _endpoints_count = 0;

    struct Remap {
        const char* from;
        const char* to;
    };

    Remap _remaps[HTTP_MAX_REMAPS]; // max 32 remaps
    int _remaps_count = 0;

    // Socket health monitoring
    uint32_t _socket_timestamps[8] = {0}; // Track when each socket became active
    uint8_t _socket_states[8] = {0};      // Previous socket states
    uint32_t _last_socket_cleanup = 0;
    uint32_t _server_restart_count = 0;
    uint8_t _server_socket = 0xFF;        // Track which socket the server is using
    
    // State machine variables
    uint32_t _last_ms = 0;
    uint32_t _state_entered_ms = 0;
    State state = WAITING;
    EthernetClient client;
    
    RestServer(EthernetServer& server) { this->server = &server; }
    void begin() { _last_socket_cleanup = millis(); }
    
    // Force close a specific socket on W5500
    void forceCloseSocket(uint8_t sock) {
        if (sock >= 8) return;
        W5100.execCmdSn(sock, Sock_CLOSE);
        W5100.writeSnIR(sock, 0xFF); // Clear all interrupt flags
        Serial.printf("[HTTP] Force closed socket %d\n", sock);
    }
    
    // Get human-readable socket status name
    const char* getSocketStatusName(uint8_t status) {
        switch(status) {
            case 0x00: return "CLOSED";
            case 0x13: return "INIT";
            case 0x14: return "LISTEN";
            case 0x15: return "SYNSENT";
            case 0x16: return "SYNRECV";
            case 0x17: return "ESTABLISHED";
            case 0x18: return "FIN_WAIT";
            case 0x1A: return "CLOSING";
            case 0x1B: return "TIME_WAIT";
            case 0x1C: return "CLOSE_WAIT";
            case 0x1D: return "LAST_ACK";
            case 0x22: return "UDP";
            default: return "UNKNOWN";
        }
    }
    
    // Check and cleanup stuck sockets
    void cleanupStuckSockets() {
        uint32_t now = millis();
        if (now - _last_socket_cleanup < HTTP_SOCKET_CLEANUP_INTERVAL_MS) return;
        
        XTP_TIMING_START(XTP_TIME_SOCKET_CLEANUP);
        _last_socket_cleanup = now;
        
        uint8_t listening_sockets = 0;
        uint8_t stuck_sockets = 0;
        
        for (uint8_t sock = 0; sock < 8; sock++) {
            uint8_t status = cyclic_sock_status(sock); //W5100.readSnSR(sock);
            uint16_t port = cyclic_sock_port(sock);
            
            // Track socket state transitions
            if (status != _socket_states[sock]) {
                _socket_timestamps[sock] = now;
                _socket_states[sock] = status;
            }
            
            uint32_t socket_age = now - _socket_timestamps[sock];
            
            // Count listening sockets on our port
            if (status == 0x14 && port == 80) { // SnSR::LISTEN = 0x14
                listening_sockets++;
                _server_socket = sock;
            }
            
            // Detect stuck sockets in transitional states
            bool is_transitional = (status == 0x18 || // FIN_WAIT
                                    status == 0x1A || // CLOSING
                                    status == 0x1B || // TIME_WAIT
                                    status == 0x1C || // CLOSE_WAIT
                                    status == 0x1D);  // LAST_ACK
            
            if (is_transitional && socket_age > HTTP_SOCKET_STALE_TIMEOUT_MS) {
                Serial.printf("[HTTP] Socket %d stuck in %s for %lu ms, forcing close\n", 
                              sock, getSocketStatusName(status), socket_age);
                forceCloseSocket(sock);
                stuck_sockets++;
            }
            
            // Also cleanup ESTABLISHED sockets that have been idle too long
            // (could indicate a client that connected but never sent data)
            if (status == 0x17 && socket_age > HTTP_SOCKET_STALE_TIMEOUT_MS * 2) {
                // Check if this is the server's accepted socket with no data
                uint16_t rx_size = W5100.readSnRX_RSR(sock);
                if (rx_size == 0) {
                    Serial.printf("[HTTP] Socket %d ESTABLISHED but idle for %lu ms, forcing close\n", 
                                  sock, socket_age);
                    forceCloseSocket(sock);
                    stuck_sockets++;
                }
            }
        }
        
        // If no listening socket on port 80, we need to restart the server
        if (listening_sockets == 0) {
            Serial.println("[HTTP] WARNING: No listening socket found! Restarting server...");
            server->begin();
            _server_restart_count++;
        }
        
        if (stuck_sockets > 0) {
            Serial.printf("[HTTP] Cleaned up %d stuck socket(s)\n", stuck_sockets);
        }
        XTP_TIMING_END(XTP_TIME_SOCKET_CLEANUP);
    }
    
    // Safe client stop with verification (non-blocking version)
    // Initiates close and returns immediately - actual close happens in state machine
    void initiateClientClose() {
        if (client) {
            client.flush();
            client.stop();
        }
        _state_entered_ms = millis();
        state = CLOSING;
    }
    
    // Force immediate client cleanup (use when we can't wait)
    void forceClientClose() {
        if (client) {
            client.flush();
            client.stop();
        }
        client = EthernetClient();
        state = WAITING;
    }
    
    // Enter a new state (tracks timing)
    void enterState(State newState) {
        state = newState;
        _state_entered_ms = millis();
    }
    
    // Time spent in current state
    uint32_t timeInState() {
        return millis() - _state_entered_ms;
    }
    
    // Debug: Print all socket statuses
    void printSocketStatus() {
        Serial.println("[HTTP] Socket Status:");
        for (uint8_t sock = 0; sock < 8; sock++) {
            uint8_t status = W5100.readSnSR(sock);
            uint16_t port = W5100.readSnPORT(sock);
            if (status != 0x00) { // Only print non-closed sockets
                Serial.printf("  Socket %d: %s (0x%02X) port:%d\n", 
                              sock, getSocketStatusName(status), status, port);
            }
        }
    }

    void on(const char* uri, HTTPMethod method, EndpointHandler handler) {
        if (_endpoints_count >= HTTP_MAX_ENDPOINTS) return; // max endpoints (for now)
        _endpoints[_endpoints_count].uri = uri;
        _endpoints[_endpoints_count].method = method;
        _endpoints[_endpoints_count].handler = handler;
        _endpoints_count++;
    }

    void get(const char* uri, EndpointHandler handler) { on(uri, HTTP_GET, handler); }
    void post(const char* uri, EndpointHandler handler) { on(uri, HTTP_POST, handler); }

    void remap(const char* from, const char* to) {
        if (_remaps_count >= HTTP_MAX_REMAPS) return; // max 32 remaps (for now)
        _remaps[_remaps_count].from = from;
        _remaps[_remaps_count].to = to;
        _remaps_count++;
    }

    const char* getMap(const char* from) {
        for (int i = 0; i < _remaps_count; i++) {
            if (_remaps[i].from == nullptr || _remaps[i].to == nullptr) continue;
            if (strcmp(_remaps[i].from, from) == 0) return _remaps[i].to;
        }
        return nullptr;
    }

    void parseMethod(char* method) {
        method[0] = '\0';
        int i = 0;
        while (client.available()) {
            char c = client.read();
            if (c == ' ' || c == '\n') break;
            if (i >= 15) break;
            method[i++] = c;
        }
        method[i] = '\0';
    }

    void parseUri(char* uri) {
        uri[0] = '\0';
        int i = 0;
        while (client.available()) {
            char c = client.read();
            if (c == ' ' || c == '\n') break;
            if (i >= 63) break;
            uri[i++] = c;
        }
        uri[i] = '\0';
    }

    int indexOf(const char* str, char c) {
        uint32_t len = strlen(str);
        for (int i = 0; i < len; i++) {
            if (str[i] == c) return i;
        }
        return -1;
    }

    const char* readHeader(const char* name) {
        for (int i = 0; i < _argc; i++) {
            if (strcmp(name, _args[i].name) == 0) return _args[i].value;
        }
        return nullptr;
    }

    // Handle incoming requests with a state machine to avoid blocking the event loop of the microcontroller
    void handleClient() {
        XTP_TIMING_START(XTP_TIME_HTTP_HANDLE);
        uint32_t t = millis();
        
        // Periodic socket health check
        cleanupStuckSockets();
        
        switch (state) {
            
        case WAITING:
            // Make sure we don't have a stale client object
            if (client) {
                forceClientClose();
            }
            
            XTP_TIMING_START(XTP_TIME_HTTP_ACCEPT);
            client = server->available();
            XTP_TIMING_END(XTP_TIME_HTTP_ACCEPT);
            if (!client) {
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            // Verify client is actually connected
            if (!client.connected()) {
                forceClientClose();
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            _ip = client.remoteIP();
            _last_ms = t;
            enterState(RECEIVING);
            break;

        case RECEIVING:
            // Check timeout
            if (t - _last_ms > HTTP_CLIENT_TIMEOUT_MS) {
                Serial.printf("[HTTP] Timeout in RECEIVING after %lu ms\n", t - _last_ms);
                _requests_failed++;
                initiateClientClose();
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            // Check if client is still connected
            if (!client.connected()) {
                Serial.println("[HTTP] Client disconnected during RECEIVING");
                forceClientClose();
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            // Wait for data to arrive (non-blocking)
            if (!client.available()) {
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            XTP_TIMING_START(XTP_TIME_HTTP_RECEIVE);
            {
                char method[16];
                Serial.printf("[%d.%d.%d.%d]: ", _ip[0], _ip[1], _ip[2], _ip[3]);
                parseMethod(method);
                parseUri(_uri);
                
                // Skip whitespace and CRLF
                while (client.available()) {
                    char c = client.peek();
                    if (c == ' ' || c == '\r' || c == '\n') {
                        client.read();
                    } else {
                        break;
                    }
                }
                
                bool is_get = strcmp(method, "GET") == 0;
                bool is_post = strcmp(method, "POST") == 0;
                if (is_get) {
                    _method = HTTP_GET;
                } else if (is_post) {
                    _method = HTTP_POST;
                } else {
                    Serial.printf("[HTTP] Unsupported method: %s\n", method);
                    client.print("HTTP/1.1 405 Method Not Allowed\r\n");
                    client.print("Connection: close\r\n");
                    client.print("\r\n");
                    _requests_failed++;
                    initiateClientClose();
                    XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                    return;
                }
                
                // Parse headers and body (optimized with bulk reads)
                if (is_get || is_post) {
                    _argc = 0;
                    uint32_t parse_deadline = t + 100;
                    
                    // Read all available data into a local buffer for faster parsing
                    char headerBuf[512];
                    int headerLen = 0;
                    int available = client.available();
                    if (available > 0) {
                        headerLen = min(available, (int)sizeof(headerBuf) - 1);
                        client.read((uint8_t*)headerBuf, headerLen);
                        headerBuf[headerLen] = '\0';
                    }
                    
                    // Parse headers from buffer
                    int pos = 0;
                    while (pos < headerLen && _argc < HTTP_MAX_ARGS) {
                        // Skip whitespace and newlines
                        while (pos < headerLen && (headerBuf[pos] == ' ' || headerBuf[pos] == '\r' || headerBuf[pos] == '\n')) {
                            pos++;
                        }
                        if (pos >= headerLen) break;
                        
                        // Check if it's a header (starts with A-Z)
                        char c = headerBuf[pos];
                        bool isHeader = c >= 'A' && c <= 'Z';
                        if (!isHeader) break;  // End of headers
                        
                        // Read header name
                        int nameStart = pos;
                        while (pos < headerLen && headerBuf[pos] != ':' && headerBuf[pos] != '\r' && headerBuf[pos] != '\n') {
                            pos++;
                        }
                        int nameLen = pos - nameStart;
                        if (nameLen > 63) nameLen = 63;
                        
                        if (pos < headerLen && headerBuf[pos] == ':') {
                            pos++;  // Skip ':'
                            // Skip leading space after colon
                            while (pos < headerLen && headerBuf[pos] == ' ') pos++;
                            
                            // Check if we should skip this header (common unneeded ones)
                            bool skipHeader = false;
                            if (nameLen == 6 && strncmp(&headerBuf[nameStart], "Accept", 6) == 0) skipHeader = true;
                            else if (nameLen == 10 && strncmp(&headerBuf[nameStart], "User-Agent", 10) == 0) skipHeader = true;
                            else if (nameLen == 10 && strncmp(&headerBuf[nameStart], "Connection", 10) == 0) skipHeader = true;
                            else if (nameLen == 15 && strncmp(&headerBuf[nameStart], "Accept-Encoding", 15) == 0) skipHeader = true;
                            else if (nameLen == 15 && strncmp(&headerBuf[nameStart], "Accept-Language", 15) == 0) skipHeader = true;
                            else if (nameLen == 13 && strncmp(&headerBuf[nameStart], "Cache-Control", 13) == 0) skipHeader = true;
                            else if (nameLen == 3 && strncmp(&headerBuf[nameStart], "DNT", 3) == 0) skipHeader = true;
                            
                            // Read value
                            int valueStart = pos;
                            while (pos < headerLen && headerBuf[pos] != '\r' && headerBuf[pos] != '\n') {
                                pos++;
                            }
                            int valueLen = pos - valueStart;
                            if (valueLen > 63) valueLen = 63;
                            
                            if (!skipHeader) {
                                // Store header
                                memcpy(_args[_argc].name, &headerBuf[nameStart], nameLen);
                                _args[_argc].name[nameLen] = '\0';
                                memcpy(_args[_argc].value, &headerBuf[valueStart], valueLen);
                                _args[_argc].value[valueLen] = '\0';
                                _argc++;
                            }
                        }
                        
                        // Skip to next line
                        while (pos < headerLen && headerBuf[pos] != '\n') pos++;
                        if (pos < headerLen) pos++;  // Skip newline
                    }
                    
                    // Read body (remaining data after headers)
                    body_length = 0;
                    // Check for body in buffer (after \r\n\r\n)
                    if (pos < headerLen) {
                        int remaining = headerLen - pos;
                        if (remaining > HTTP_MAX_BODY_SIZE) remaining = HTTP_MAX_BODY_SIZE;
                        memcpy(body, &headerBuf[pos], remaining);
                        body_length = remaining;
                    }
                    // Read any additional body data from socket
                    uint32_t bodyDeadline = millis() + 20;  // Short timeout for body
                    while (client.available() && body_length < HTTP_MAX_BODY_SIZE && millis() < bodyDeadline) {
                        int toRead = min(client.available(), HTTP_MAX_BODY_SIZE - body_length);
                        body_length += client.read((uint8_t*)&body[body_length], toRead);
                    }
                    body[body_length] = '\0';
                }
            }
            XTP_TIMING_END(XTP_TIME_HTTP_RECEIVE);
            _last_ms = t;
            enterState(PROCESSING);
            break;

        case PROCESSING:
            // Check timeout
            if (t - _last_ms > HTTP_CLIENT_TIMEOUT_MS) {
                Serial.printf("[HTTP] Timeout in PROCESSING after %lu ms\n", t - _last_ms);
                _requests_failed++;
                initiateClientClose();
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            // Verify client is still connected
            if (!client.connected()) {
                Serial.println("[HTTP] Client disconnected before processing");
                forceClientClose();
                XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
                return;
            }
            
            // Find matching endpoint
            {
                bool found = false;
                for (int i = 0; i < _endpoints_count; i++) {
                    auto& endpoint = _endpoints[i];
                    auto uri = endpoint.uri;
                    auto method = endpoint.method;
                    bool uri_match = strcmp(uri, _uri) == 0;
                    if (!uri_match) {
                        const char* alt_uri = getMap(_uri);
                        if (alt_uri != nullptr)
                            uri_match = strcmp(uri, alt_uri) == 0;
                    }
                    bool method_match = method == _method;
                    
                    if (uri_match && method_match) {
                        _requests_success++;
                        _transmitted_bytes = 0;
                        Serial.printf("  %s %s", method == HTTP_GET ? "GET" : "POST", uri);
                        uint32_t handler_start = millis();
                        
                        // Execute handler
                        XTP_TIMING_START(XTP_TIME_HTTP_HANDLER);
                        endpoint.handler();
                        XTP_TIMING_END(XTP_TIME_HTTP_HANDLER);
                        
                        uint32_t elapsed_ms = millis() - handler_start;
                        Serial.printf(" - %u bytes in %lu ms\n", _transmitted_bytes, elapsed_ms);
                        
                        found = true;
                        initiateClientClose();
                        break;
                    }
                }
                
                if (!found) {
                    enterState(FAILED);
                }
            }
            break;

        case FAILED:
            _requests_failed++;
            Serial.printf("  %s %s - 404 Not Found\n", _method == HTTP_GET ? "GET" : "POST", _uri);
            
            if (client.connected()) {
                if (_notFoundHandler_defined) {
                    _notFoundHandler();
                } else {
                    client.print("HTTP/1.1 404 Not Found\r\n");
                    client.print("Content-Type: text/plain\r\n");
                    client.print("Connection: close\r\n");
                    client.print("\r\n");
                    client.print("Error 404, page not found");
                }
            }
            initiateClientClose();
            break;

        case CLOSING:
            XTP_TIMING_START(XTP_TIME_HTTP_CLOSE);
            // Non-blocking graceful close - wait up to 50ms
            if (!client.connected() || timeInState() >= 50) {
                if (client.connected()) {
                    // Still connected after timeout - force close
                    XTP_TIMING_END(XTP_TIME_HTTP_CLOSE);
                    enterState(FORCE_CLOSING);
                } else {
                    // Clean disconnect
                    client = EthernetClient();
                    XTP_TIMING_END(XTP_TIME_HTTP_CLOSE);
                    enterState(WAITING);
                }
            } else {
                XTP_TIMING_END(XTP_TIME_HTTP_CLOSE);
            }
            break;

        case FORCE_CLOSING:
            // Force immediate close
            if (client) {
                Serial.println("[HTTP] Force closing stuck client");
            }
            client = EthernetClient();
            enterState(WAITING);
            break;
            
        } // switch(state)
        XTP_TIMING_END(XTP_TIME_HTTP_HANDLE);
    } // handleClient
    
    // Get server statistics
    void getStats(uint32_t& success, uint32_t& failed, uint32_t& restarts) {
        success = _requests_success;
        failed = _requests_failed;
        restarts = _server_restart_count;
    }

    void send(int code, const char* content_type, const char* content, int length) {
        XTP_TIMING_START(XTP_TIME_HTTP_SEND);
        // Using batched response in chunks of HTTP_RES_CHUNK_SIZE bytes
        for (int i = 0; i < length; i += HTTP_RES_CHUNK_SIZE) {
            int len = length - i;
            if (len > HTTP_RES_CHUNK_SIZE) len = HTTP_RES_CHUNK_SIZE;
            if (i == 0) sendHeader(code, content_type, length);
            client.write((const uint8_t*) &content[i], len);
            bool done = i + len >= length;
        }
        _transmitted_bytes += length;
        XTP_TIMING_END(XTP_TIME_HTTP_SEND);
        // client.stop();
    }

    void sendBuffer(int code, const uint8_t* buffer, int length) {
        send(code, "application/octet-stream", (const char*) buffer, length);
    }


    template <typename T> void send(int code, T& DB) {
        int length = sizeof(T);
        sendBuffer(code, (const uint8_t*) &DB, length);
    }

    void send(int code, const char* content_type, const char* content) {
        send(code, content_type, content, strlen(content));
    }


    void sendHeader(int code, const char* content_type, int length = -1) {
        client.printf("HTTP/1.1 %d %s\r\n", code, code >= 300 ? "NOT OK" : "OK");
        client.printf("Content-Type: %s\r\n", content_type);
        if (length >= 0) client.printf("Content-Length: %d\r\n", length);
        client.printf("Connection: close\r\n\r\n");
    }

    void write(uint8_t* buffer, int length) {
        client.write(buffer, length);
    }

    void end() {
        client.stop();
    }

    String uri() { return _uri; }
    HTTPMethod method() { return _method; }
    int args() { return _argc; }
    Argument arg(int i) { return _args[i]; }
    const char* argName(int i) { return (const char*) _args[i].name; }
    const char* argValue(int i) { return (const char*) _args[i].value; }
    void onNotFound(EndpointHandler handler) { _notFoundHandler = handler; _notFoundHandler_defined = true; }
};



bool startsWith(const char* line, const char* prefix, bool case_sensitive = true) {
    int line_len = strlen(line);
    int prefix_len = strlen(prefix);
    if (line_len < prefix_len) return false;
    for (int i = 0; i < prefix_len; i++) {
        char a = case_sensitive ? line[i] : toUpperCase(line[i]);
        char b = case_sensitive ? prefix[i] : toUpperCase(prefix[i]);
        if (a != b) return false;
    }
    return true;
}

bool endsWith(const char* line, const char* postfix, bool case_sensitive = true) {
    int line_len = strlen(line);
    int postfix_len = strlen(postfix);
    if (line_len < postfix_len) return false;
    int i = line_len - postfix_len;
    for (int j = 0; j < postfix_len; j++) {
        char a = case_sensitive ? line[i + j] : toUpperCase(line[i + j]);
        char b = case_sensitive ? postfix[j] : toUpperCase(postfix[j]);
        if (a != b) return false;
    }
    return true;
}

const char* file_content_type(const char* file_name) {
    if (endsWith(file_name, ".html", false)) return "text/html";
    if (endsWith(file_name, ".css", false)) return "text/css";
    if (endsWith(file_name, ".js", false)) return "application/javascript";
    if (endsWith(file_name, ".json", false)) return "application/json";
    if (endsWith(file_name, ".png", false)) return "image/png";
    if (endsWith(file_name, ".jpg", false)) return "image/jpeg";
    if (endsWith(file_name, ".jpeg", false)) return "image/jpeg";
    if (endsWith(file_name, ".gif", false)) return "image/gif";
    if (endsWith(file_name, ".ico", false)) return "image/x-icon";
    if (endsWith(file_name, ".svg", false)) return "image/svg+xml";
    if (endsWith(file_name, ".ttf", false)) return "application/x-font-ttf";
    if (endsWith(file_name, ".otf", false)) return "application/x-font-otf";
    if (endsWith(file_name, ".woff", false)) return "application/font-woff";
    if (endsWith(file_name, ".woff2", false)) return "application/font-woff2";
    if (endsWith(file_name, ".eot", false)) return "application/vnd.ms-fontobject";
    if (endsWith(file_name, ".mp3", false)) return "audio/mpeg";
    if (endsWith(file_name, ".mp4", false)) return "video/mp4";
    if (endsWith(file_name, ".m4a", false)) return "audio/mp4";
    if (endsWith(file_name, ".m4v", false)) return "video/mp4";
    if (endsWith(file_name, ".mov", false)) return "video/quicktime";
    if (endsWith(file_name, ".webm", false)) return "video/webm";
    if (endsWith(file_name, ".wav", false)) return "audio/wav";
    if (endsWith(file_name, ".flac", false)) return "audio/flac";
    if (endsWith(file_name, ".opus", false)) return "audio/opus";
    if (endsWith(file_name, ".ogg", false)) return "audio/ogg";
    if (endsWith(file_name, ".ogv", false)) return "video/ogg";
    if (endsWith(file_name, ".ogm", false)) return "video/ogg";
    if (endsWith(file_name, ".ogx", false)) return "application/ogg";
    return "text/plain";
}

class MyFile {
private:
    char* _name;
    int32_t _length;
    char* _data;
public:
    MyFile(const char* name, const char* data, int32_t length = -1) {
        _name = (char*) name;
        _length = length;
        if (_length == -1) _length = strlen(data);
        _data = (char*) data;
    }
    const char* name() { return _name; }
    int32_t length() { return _length; }
    const char* data() { return _data; }
};

class MyFileSystem {
private:
    bool __file_route_logging = false;
    int32_t _numOfFiles = 0;
    MyFile* _files[30];
public:
    MyFileSystem() {}
    void addFile(const char* name, const char* data, int32_t length = -1) {
        if (!__file_route_logging) {
            __file_route_logging = true;
            Serial.println("Routing files to web server");
        }
#ifdef ESP8266
        length = length < 0 ? strlen_P((PGM_P) data) : length;
#else
        length = length < 0 ? strlen(data) : length;
#endif
        Serial.printf("  - \"%s\"  -> %d\n", name, length);
        if (_numOfFiles >= 30) return;
        _files[_numOfFiles++] = new MyFile(name, data, length);
    }
    MyFile* getFile(const char* name) {
        for (int32_t i = 0; i < _numOfFiles; i++) {
            const char* name_existing = _files[i]->name();
            if (startsWith(name_existing, name, true)) return _files[i];
        }
        return NULL;
    }

    void handleGetFile(RestServer& rest, const char* file_name) {
        MyFile* file = this->getFile(file_name);
        if (file == NULL) {
            rest.send(404, "File Not Found");
            return;
        }
        rest.send(200, file_content_type(file_name), file->data(), file->length());
    }
};
