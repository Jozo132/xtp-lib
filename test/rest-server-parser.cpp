#include <assert.h>
#include <string.h>

#include "../src/rest_server_request_parser.h"

namespace {

void appendChunk(RestServerReceiveState& state, const char* chunk, char* body, int& body_length) {
    restServerAppendChunk(state, chunk, (int) strlen(chunk), body, HTTP_MAX_BODY_SIZE, body_length);
}

void requireField(const RestServerRequestField* fields, int count, const char* name, const char* value) {
    for (int i = 0; i < count; i++) {
        if (strcmp(fields[i].name, name) == 0) {
            assert(strcmp(fields[i].value, value) == 0);
            return;
        }
    }
    assert(false);
}

void testJsonPostWithContentLength() {
    RestServerReceiveState state;
    restServerResetReceiveState(state);

    RestServerRequestField headers[HTTP_MAX_HEADERS];
    RestServerRequestField args[HTTP_MAX_ARGS];
    char method[16];
    char path[64];
    char body[HTTP_MAX_BODY_SIZE + 1] = {0};
    int body_length = 0;
    int header_count = 0;
    int arg_count = 0;
    int expected_body_length = 0;

    appendChunk(state,
                "POST /api/config HTTP/1.1\r\nhost: example\r\ncontent-type: application/json\r\ncontent-length: 15\r\n\r\n{\"output0\":\"1\"}",
                body, body_length);

    assert(state.headers_complete);
    assert(restServerParseRequestHead(state, method, sizeof(method), path, sizeof(path), headers, HTTP_MAX_HEADERS, header_count, args, HTTP_MAX_ARGS, arg_count, expected_body_length));
    state.expected_body_length = expected_body_length;

    assert(strcmp(method, "POST") == 0);
    assert(strcmp(path, "/api/config") == 0);
    assert(arg_count == 0);
    assert(header_count == 3);
    assert(expected_body_length == 15);
    assert(restServerRequestComplete(state));
    assert(body_length == 15);
    assert(strcmp(body, "{\"output0\":\"1\"}") == 0);
    requireField(headers, header_count, "host", "example");
    requireField(headers, header_count, "content-type", "application/json");
}

void testSplitArrivalWaitsForFullBody() {
    RestServerReceiveState state;
    restServerResetReceiveState(state);

    RestServerRequestField headers[HTTP_MAX_HEADERS];
    RestServerRequestField args[HTTP_MAX_ARGS];
    char method[16];
    char path[64];
    char body[HTTP_MAX_BODY_SIZE + 1] = {0};
    int body_length = 0;
    int header_count = 0;
    int arg_count = 0;
    int expected_body_length = 0;

    appendChunk(state,
                "POST /api/config?foo=bar HTTP/1.1\r\nhost: example\r\ncontent-type: application/json\r\ncontent-length: 15\r",
                body, body_length);
    assert(!state.headers_complete);
    assert(body_length == 0);

    appendChunk(state, "\n\r\n{\"out", body, body_length);
    assert(state.headers_complete);
    assert(restServerParseRequestHead(state, method, sizeof(method), path, sizeof(path), headers, HTTP_MAX_HEADERS, header_count, args, HTTP_MAX_ARGS, arg_count, expected_body_length));
    state.expected_body_length = expected_body_length;

    assert(strcmp(path, "/api/config") == 0);
    assert(arg_count == 1);
    requireField(args, arg_count, "foo", "bar");
    assert(!restServerRequestComplete(state));
    assert(body_length == 5);

    appendChunk(state, "put0\":\"1\"}", body, body_length);
    assert(restServerRequestComplete(state));
    assert(body_length == 15);
    assert(strcmp(body, "{\"output0\":\"1\"}") == 0);
}

void testQueryArgsStaySeparateFromHeaders() {
    RestServerReceiveState state;
    restServerResetReceiveState(state);

    RestServerRequestField headers[HTTP_MAX_HEADERS];
    RestServerRequestField args[HTTP_MAX_ARGS];
    char method[16];
    char path[64];
    char body[HTTP_MAX_BODY_SIZE + 1] = {0};
    int body_length = 0;
    int header_count = 0;
    int arg_count = 0;
    int expected_body_length = 0;

    appendChunk(state,
                "GET /api/status?foo=bar&x=1 HTTP/1.1\r\nHost: device\r\nContent-Type: application/json\r\n\r\n",
                body, body_length);

    assert(restServerParseRequestHead(state, method, sizeof(method), path, sizeof(path), headers, HTTP_MAX_HEADERS, header_count, args, HTTP_MAX_ARGS, arg_count, expected_body_length));
    state.expected_body_length = expected_body_length;

    assert(strcmp(method, "GET") == 0);
    assert(strcmp(path, "/api/status") == 0);
    assert(restServerRequestComplete(state));
    assert(body_length == 0);
    assert(arg_count == 2);
    requireField(args, arg_count, "foo", "bar");
    requireField(args, arg_count, "x", "1");
    requireField(headers, header_count, "Host", "device");
    requireField(headers, header_count, "Content-Type", "application/json");
}

void testFormBodyAddsRequestArgs() {
    RestServerReceiveState state;
    restServerResetReceiveState(state);

    RestServerRequestField headers[HTTP_MAX_HEADERS];
    RestServerRequestField args[HTTP_MAX_ARGS];
    char method[16];
    char path[64];
    char body[HTTP_MAX_BODY_SIZE + 1] = {0};
    int body_length = 0;
    int header_count = 0;
    int arg_count = 0;
    int expected_body_length = 0;

    appendChunk(state,
                "POST /api/form?mode=save HTTP/1.1\r\ncontent-type: application/x-www-form-urlencoded; charset=utf-8\r\ncontent-length: 7\r\n\r\na=1&b=2",
                body, body_length);

    assert(restServerParseRequestHead(state, method, sizeof(method), path, sizeof(path), headers, HTTP_MAX_HEADERS, header_count, args, HTTP_MAX_ARGS, arg_count, expected_body_length));
    state.expected_body_length = expected_body_length;
    assert(restServerRequestComplete(state));

    restServerParseFormBody(body, body_length, args, HTTP_MAX_ARGS, arg_count);
    assert(strcmp(path, "/api/form") == 0);
    assert(arg_count == 3);
    requireField(args, arg_count, "mode", "save");
    requireField(args, arg_count, "a", "1");
    requireField(args, arg_count, "b", "2");
}

}  // namespace

int main() {
    testJsonPostWithContentLength();
    testSplitArrivalWaitsForFullBody();
    testQueryArgsStaySeparateFromHeaders();
    testFormBodyAddsRequestArgs();
    return 0;
}