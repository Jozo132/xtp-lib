#pragma once


#include <Ethernet.h>

#define HTTP_MAX_ARGS 32
#define HTTP_MAX_ENDPOINTS 32
#define HTTP_MAX_REMAPS 32
#define HTTP_MAX_BODY_SIZE 1024

#ifndef HTTP_RES_CHUNK_SIZE
#define HTTP_RES_CHUNK_SIZE 2048
#endif /* HTTP_RES_CHUNK_SIZE */

enum HTTPMethod { HTTP_GET, HTTP_POST };

// DIY implementation of a REST server
class RestServer {
public:
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

    RestServer(EthernetServer& server) { this->server = &server; }
    void begin() {}

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
    enum State { WAITING, RECEIVING, PROCESSING, HANDLING, FAILED };
    uint32_t _last_ms = 0;
    State state = WAITING;
    EthernetClient client;
    void handleClient() {
        uint32_t t = millis();
        uint32_t elapsed = t - _last_ms;
        if (state != WAITING && elapsed > 100) {
            Serial.println("HTTP server timeout");
            client.stop();
            state = WAITING;
        }

        if (!client) {
            client = server->available();
            if (!client) return;
            // Serial.println("Client connected");
            _ip = client.remoteIP();
            _last_ms = t;
            state = RECEIVING;
        }

        if (state == RECEIVING) {
            if (!client.available()) return;
            char method[16];
            Serial.printf("[%d.%d.%d.%d]: ", _ip[0], _ip[1], _ip[2], _ip[3]);
            parseMethod(method);
            // Serial.println(method);
            parseUri(_uri);
            if (client.available() && client.peek() == ' ') client.read();
            if (client.available() && client.peek() == '\r') client.read();
            if (client.available() && client.peek() == '\n') client.read();
            bool is_get = strcmp(method, "GET") == 0;
            bool is_post = strcmp(method, "POST") == 0;
            if (is_get) {
                _method = HTTP_GET;
            } else if (is_post) {
                _method = HTTP_POST;
            } else {
                Serial.printf("Unsupported method %s\n", method);
                client.stop();
                state = WAITING;
                return;
            }
            delay(5);
            // Extract arguments
            if ((is_get || is_post)) {
                _argc = 0;
                uint32_t timeout = millis() + 100;
                while (client.available()) {
                    if (millis() > timeout) break;
                    char line[65];
                    char c = client.peek();
                    while (c && (c == ' ' || c == '\r' || c == '\n')) {
                        if (millis() > timeout) break;
                        client.read();
                        c = client.peek(); // Skip spaces, CR, LF
                    }
                    bool isHeader = c >= 'A' && c <= 'Z';
                    if (!isHeader) break; // End of headers, start of body
                    int i = 0;
                    while (client.available()) {
                        if (millis() > timeout) break;
                        c = client.read();
                        if (i == 0 && c == ' ') break;
                        if (c == '\r' || c == '\n' || c == ':') break;
                        line[i] = c;
                        i++;
                        if (i >= 64) break;
                    }
                    line[i] = '\0';
                    int name_len = i;
                    // If the first character is a capital letter, it's a header
                    // int pos = line.indexOf(':');
                    if (c == ':') {
                        char* name = (char*) _args[_argc].name;
                        char* value = (char*) _args[_argc].value;
                        for (int i = 0; i < name_len; i++)
                            name[i] = line[i];
                        name[name_len] = '\0';

                        // Ignore: "Accept", "Accept-Encoding", "Accept-Language", "Cache-Control", "Connection", "DNT", "User-Agent"
                        if (strcmp(name, "Accept") == 0 || strcmp(name, "Accept-Encoding") == 0 || strcmp(name, "Accept-Language") == 0 || strcmp(name, "Cache-Control") == 0 || strcmp(name, "Connection") == 0 || strcmp(name, "DNT") == 0 || strcmp(name, "User-Agent") == 0 || strcmp(name, "Upgrade-Insecure-Requests") == 0) {
                            // Consume the rest of the line, so we can read the next one
                            while (client.available()) {
                                if (millis() > timeout) break;
                                c = client.read();
                                if (c == '\n' || c == '\r') break;
                            }
                            continue;
                        }

                        int i = 0;
                        uint32_t timeout = millis() + 100;
                        bool failed = false;
                        while (client.available()) {
                            if (millis() > timeout) {
                                failed = true;
                                break;
                            };
                            if (!client.available()) continue;
                            c = client.read();
                            if (c == '\n' || c == '\r') break;
                            value[i++] = c;
                            if (i >= 64) break;
                        }

                        if (failed) break;

                        value[i] = '\0';

                        // Serial.printf("  %s: ", name);
                        // Serial.println(value);
                        _argc++;
                        if (_argc >= HTTP_MAX_ARGS) break;
                    }
                }

                timeout = millis() + 100;
                body_length = 0;
                while (client.available()) {
                    if (millis() > timeout) break;
                    if (body_length >= HTTP_MAX_BODY_SIZE) break;
                    body[body_length++] = client.read();
                }
                body[body_length] = '\0';
            }
            _last_ms = t;
            state = PROCESSING;
        }

        if (state == PROCESSING) {
            state = FAILED; // Assume failure if no endpoint is found
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
                    u32 t = millis();
                    endpoint.handler();
                    client.stop();
                    u32 elapsed_ms = millis() - t;
                    Serial.printf(" - %u bytes in %u ms\n", _transmitted_bytes, elapsed_ms);
                    state = WAITING;
                    break;
                }
            }
        }

        if (state == FAILED) {
            _requests_failed++;
            Serial.printf("  %s %s - 404 Not Found\n", _method == HTTP_GET ? "GET" : "POST", _uri);
            if (_notFoundHandler_defined) {
                _notFoundHandler();
            } else {
                client.println("HTTP/1.1 404 Not Found");
                client.println("Content-Type: text/plain");
                client.println("Connection: close");
                client.println();
                client.println("Error 404, page not found");
                client.stop();
            }
            state = WAITING;
        }
    } // handleClient

    void send(int code, const char* content_type, const char* content, int length) {
        // Using batched response in chunks of HTTP_RES_CHUNK_SIZE bytes
        for (int i = 0; i < length; i += HTTP_RES_CHUNK_SIZE) {
            int len = length - i;
            if (len > HTTP_RES_CHUNK_SIZE) len = HTTP_RES_CHUNK_SIZE;
            if (i == 0) sendHeader(code, content_type, length);
            client.write((const uint8_t*) &content[i], len);
            bool done = i + len >= length;
        }
        _transmitted_bytes += length;
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
        client.printf("HTTP/1.1 %d %s\n", code, code >= 300 ? "NOT OK" : "OK");
        client.printf("Content-Type: %s\n", content_type);
        if (length >= 0) client.printf("Content-Length: %d\n", length);
        client.printf("Connection: close\n\n");
        // client.printf("Connection: keep-alive\n");
        // client.printf("Keep-Alive: timeout=5\n\n");
        // client.setConnectionTimeout(5000);
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



bool startsWith(const char* line, const char* prefix, bool case_sensitive) {
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
