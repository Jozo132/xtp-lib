#pragma once

#include "rest_server.h"

RestServer rest(server);
MyFileSystem files; // Todo: add implementation

#define REST_SERVE_FILE(name, data, length) files.addFile(name, data, length); rest.get(name, []() { files.handleGetFile(rest, name); });

char project_info[512] = { 0 };
char rest_response_basic[512] = "";

void (*rest_setup)() = nullptr;

/**
 * @example
 *  const char* test_text = "Hello, World!";
 *  struct test_struct_t {
 *      const char* name;
 *      const char* lastname;
 *      int age;
 *  };
 *  test_struct_t test_struct = { "Janez", "Novak", 42 };
 *  // GET plain text
 *  rest.get("/test_text", []() { rest.send(200, test_text); });
 *  // GET struct as a buffer (requires manual parsing on the client side)
 *  rest.get("/test_struct", []() { rest.send(200, test_struct); });
 *  // GET JSON body
 *  rest.get("/basic", []() {
 *      json_buffer.clear();
 *      json_buffer["name"] = test_struct.name;
 *      json_buffer["lastname"] = test_struct.lastname;
 *      json_buffer["age"] = test_struct.age;
 *      serializeJson(json_buffer, rest_response_basic);
 *      rest.send(200, "application/json", rest_response_basic);
 *  });
**/

bool xtp_rest_routing_initialized = false;

// Buffer for socket status JSON response
char socket_status_json[512] = "";
char eth_status_buffer[384] = "";

void xtp_rest_routing() {
    if (xtp_rest_routing_initialized) return;
    xtp_rest_routing_initialized = true;

    // Remap root to index.html
    rest.remap("/", "/index.html");

    // Simple ping/pong
    rest.get("/ping", []() { rest.send(200, "text/plain", "pong"); });
    
    // Ethernet state machine status endpoint
    rest.get("/api/network-status", []() {
        ethernet_status_json(eth_status_buffer, sizeof(eth_status_buffer));
        rest.send(200, "application/json", eth_status_buffer);
    });
    
    // Diagnostic endpoint for socket status monitoring
    rest.get("/api/socket-status", []() {
        uint32_t success, failed, restarts;
        rest.getStats(success, failed, restarts);
        
        int offset = sprintf(socket_status_json, 
            "{\"requests\":{\"success\":%lu,\"failed\":%lu},\"server_restarts\":%lu,\"sockets\":[",
            success, failed, restarts);
        
        for (uint8_t sock = 0; sock < 8; sock++) {
            uint8_t status = cyclic_sock_status(sock);
            uint16_t port = cyclic_sock_port(sock);
            offset += sprintf(socket_status_json + offset, 
                "%s{\"id\":%d,\"status\":\"%s\",\"port\":%d}",
                sock > 0 ? "," : "",
                sock, rest.getSocketStatusName(status), port);
        }
        sprintf(socket_status_json + offset, "]}");
        
        rest.send(200, "application/json", socket_status_json);
    });

    // User provided setup function
    if (rest_setup != nullptr) rest_setup();

    rest.onNotFound([]() { rest.send(404, "text/plain", "Ta stran ne obstaja!"); });
}

bool web_server_initialized = false;
void web_server_setup() {
    if (web_server_initialized) return;
    web_server_initialized = true;
#ifdef USE_REST_API_SERVER
    char project_name[64] = { 0 };
    getFileNameFromPath(project_path, project_name, 64);
    sprintf(project_info, "Project: %s\nBuild: %s\nCompiled: %s %s", project_name, build_number, project_date, project_time);

    //route_files(); // Boundled front-end files
    xtp_rest_routing();
    Serial.println("HTTP rest server started");
#endif
}

void web_server_loop() {
#ifdef USE_REST_API_SERVER
    rest.handleClient();
#endif
}