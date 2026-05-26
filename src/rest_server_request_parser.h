#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef HTTP_MAX_ARGS
#define HTTP_MAX_ARGS 32
#endif

#ifndef HTTP_MAX_HEADERS
#define HTTP_MAX_HEADERS 32
#endif

#ifndef HTTP_MAX_BODY_SIZE
#define HTTP_MAX_BODY_SIZE 1024
#endif

#ifndef HTTP_MAX_HEADER_SIZE
#define HTTP_MAX_HEADER_SIZE 512
#endif

struct RestServerRequestField {
    char name[64];
    char value[64];
};

struct RestServerReceiveState {
    char header_buffer[HTTP_MAX_HEADER_SIZE + 1];
    int header_length;
    bool headers_complete;
    bool header_overflow;
    int expected_body_length;
    int received_body_length;
};

inline void restServerResetReceiveState(RestServerReceiveState& state) {
    state.header_length = 0;
    state.headers_complete = false;
    state.header_overflow = false;
    state.expected_body_length = 0;
    state.received_body_length = 0;
    state.header_buffer[0] = '\0';
}

inline bool restServerCharEqualsIgnoreCase(char left, char right) {
    return tolower((unsigned char) left) == tolower((unsigned char) right);
}

inline bool restServerEqualsIgnoreCase(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    while (*left != '\0' && *right != '\0') {
        if (!restServerCharEqualsIgnoreCase(*left, *right)) return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

inline bool restServerStartsWithIgnoreCase(const char* value, const char* prefix) {
    if (value == nullptr || prefix == nullptr) return false;
    while (*prefix != '\0') {
        if (*value == '\0') return false;
        if (!restServerCharEqualsIgnoreCase(*value, *prefix)) return false;
        value++;
        prefix++;
    }
    return true;
}

inline int restServerFindHeaderEnd(const char* buffer, int length) {
    for (int i = 0; i + 3 < length; i++) {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n' && buffer[i + 2] == '\r' && buffer[i + 3] == '\n') {
            return i + 4;
        }
    }
    for (int i = 0; i + 1 < length; i++) {
        if (buffer[i] == '\n' && buffer[i + 1] == '\n') {
            return i + 2;
        }
    }
    return -1;
}

inline int restServerHexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char) tolower((unsigned char) c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

inline void restServerCopyTrimmed(char* destination, int destination_size, const char* source, int source_length) {
    if (destination_size <= 0) return;

    int start = 0;
    int end = source_length;
    while (start < end && (source[start] == ' ' || source[start] == '\t' || source[start] == '\r' || source[start] == '\n')) {
        start++;
    }
    while (end > start && (source[end - 1] == ' ' || source[end - 1] == '\t' || source[end - 1] == '\r' || source[end - 1] == '\n')) {
        end--;
    }

    int copy_length = end - start;
    if (copy_length > destination_size - 1) copy_length = destination_size - 1;
    if (copy_length > 0) memcpy(destination, source + start, copy_length);
    destination[copy_length] = '\0';
}

inline void restServerCopyDecoded(char* destination, int destination_size, const char* source, int source_length) {
    if (destination_size <= 0) return;

    int destination_index = 0;
    for (int source_index = 0; source_index < source_length && destination_index < destination_size - 1; source_index++) {
        char c = source[source_index];
        if (c == '+') {
            destination[destination_index++] = ' ';
            continue;
        }

        if (c == '%' && source_index + 2 < source_length) {
            int high = restServerHexValue(source[source_index + 1]);
            int low = restServerHexValue(source[source_index + 2]);
            if (high >= 0 && low >= 0) {
                destination[destination_index++] = (char) ((high << 4) | low);
                source_index += 2;
                continue;
            }
        }

        destination[destination_index++] = c;
    }

    destination[destination_index] = '\0';
}

inline void restServerStoreField(RestServerRequestField* fields, int max_fields, int& field_count,
                                 const char* name, int name_length,
                                 const char* value, int value_length,
                                 bool decode_value) {
    if (field_count >= max_fields) return;

    if (decode_value) {
        restServerCopyDecoded(fields[field_count].name, sizeof(fields[field_count].name), name, name_length);
        restServerCopyDecoded(fields[field_count].value, sizeof(fields[field_count].value), value, value_length);
    } else {
        restServerCopyTrimmed(fields[field_count].name, sizeof(fields[field_count].name), name, name_length);
        restServerCopyTrimmed(fields[field_count].value, sizeof(fields[field_count].value), value, value_length);
    }
    field_count++;
}

inline void restServerParseParameterString(const char* source, int source_length,
                                           RestServerRequestField* fields, int max_fields, int& field_count) {
    int position = 0;
    while (position <= source_length && field_count < max_fields) {
        int segment_start = position;
        while (position < source_length && source[position] != '&') {
            position++;
        }

        int segment_length = position - segment_start;
        if (segment_length > 0) {
            int separator = -1;
            for (int i = 0; i < segment_length; i++) {
                if (source[segment_start + i] == '=') {
                    separator = i;
                    break;
                }
            }

            if (separator >= 0) {
                restServerStoreField(fields, max_fields, field_count,
                                     source + segment_start, separator,
                                     source + segment_start + separator + 1, segment_length - separator - 1,
                                     true);
            } else {
                restServerStoreField(fields, max_fields, field_count,
                                     source + segment_start, segment_length,
                                     "", 0,
                                     true);
            }
        }

        position++;
    }
}

inline void restServerParseRequestTarget(const char* request_target, int target_length,
                                         char* path, int path_size,
                                         RestServerRequestField* args, int max_args, int& arg_count) {
    int query_index = -1;
    for (int i = 0; i < target_length; i++) {
        if (request_target[i] == '?') {
            query_index = i;
            break;
        }
    }

    int path_length = query_index >= 0 ? query_index : target_length;
    if (path_length == 0) {
        restServerCopyTrimmed(path, path_size, "/", 1);
    } else {
        restServerCopyTrimmed(path, path_size, request_target, path_length);
    }

    if (query_index >= 0 && query_index + 1 < target_length) {
        restServerParseParameterString(request_target + query_index + 1, target_length - query_index - 1,
                                       args, max_args, arg_count);
    }
}

inline bool restServerParseRequestHead(const RestServerReceiveState& state,
                                       char* method, int method_size,
                                       char* path, int path_size,
                                       RestServerRequestField* headers, int max_headers, int& header_count,
                                       RestServerRequestField* args, int max_args, int& arg_count,
                                       int& expected_body_length) {
    if (!state.headers_complete) return false;

    if (method_size > 0) method[0] = '\0';
    if (path_size > 0) path[0] = '\0';
    header_count = 0;
    arg_count = 0;
    expected_body_length = 0;

    int line_end = 0;
    while (line_end < state.header_length && state.header_buffer[line_end] != '\n') {
        line_end++;
    }

    int request_line_length = line_end;
    if (request_line_length > 0 && state.header_buffer[request_line_length - 1] == '\r') {
        request_line_length--;
    }
    if (request_line_length <= 0) return false;

    int first_space = -1;
    int second_space = -1;
    for (int i = 0; i < request_line_length; i++) {
        if (state.header_buffer[i] == ' ') {
            if (first_space < 0) {
                first_space = i;
            } else {
                second_space = i;
                break;
            }
        }
    }
    if (first_space <= 0) return false;

    restServerCopyTrimmed(method, method_size, state.header_buffer, first_space);

    int target_start = first_space + 1;
    int target_end = second_space >= 0 ? second_space : request_line_length;
    if (target_end <= target_start) return false;
    restServerParseRequestTarget(state.header_buffer + target_start, target_end - target_start,
                                 path, path_size, args, max_args, arg_count);

    int position = line_end < state.header_length ? line_end + 1 : state.header_length;
    while (position < state.header_length) {
        int next_line = position;
        while (next_line < state.header_length && state.header_buffer[next_line] != '\n') {
            next_line++;
        }

        int line_length = next_line - position;
        if (line_length > 0 && state.header_buffer[position + line_length - 1] == '\r') {
            line_length--;
        }

        if (line_length > 0) {
            int separator = -1;
            for (int i = 0; i < line_length; i++) {
                if (state.header_buffer[position + i] == ':') {
                    separator = i;
                    break;
                }
            }

            if (separator > 0) {
                restServerStoreField(headers, max_headers, header_count,
                                     state.header_buffer + position, separator,
                                     state.header_buffer + position + separator + 1, line_length - separator - 1,
                                     false);

                const RestServerRequestField& header = headers[header_count - 1];
                if (restServerEqualsIgnoreCase(header.name, "Content-Length")) {
                    long content_length = strtol(header.value, nullptr, 10);
                    expected_body_length = content_length > 0 ? (int) content_length : 0;
                }
            }
        }

        position = next_line < state.header_length ? next_line + 1 : state.header_length;
    }

    return true;
}

inline void restServerStoreBodyBytes(RestServerReceiveState& state,
                                     const char* data, int data_length,
                                     char* body, int body_capacity, int& stored_body_length) {
    if (data_length <= 0) return;

    state.received_body_length += data_length;

    int available_space = body_capacity - stored_body_length;
    if (available_space < 0) available_space = 0;
    int copy_length = data_length < available_space ? data_length : available_space;
    if (copy_length > 0) {
        memcpy(body + stored_body_length, data, copy_length);
        stored_body_length += copy_length;
    }
    body[stored_body_length] = '\0';
}

inline void restServerAppendChunk(RestServerReceiveState& state,
                                  const char* data, int data_length,
                                  char* body, int body_capacity, int& stored_body_length) {
    if (data_length <= 0 || state.header_overflow) return;

    int offset = 0;
    while (offset < data_length && !state.headers_complete) {
        if (state.header_length >= HTTP_MAX_HEADER_SIZE) {
            state.header_overflow = true;
            body[stored_body_length] = '\0';
            return;
        }

        state.header_buffer[state.header_length++] = data[offset++];
        state.header_buffer[state.header_length] = '\0';

        int header_end = restServerFindHeaderEnd(state.header_buffer, state.header_length);
        if (header_end >= 0) {
            state.headers_complete = true;
            state.header_length = header_end;
            state.header_buffer[state.header_length] = '\0';
        }
    }

    if (state.headers_complete && offset < data_length) {
        restServerStoreBodyBytes(state, data + offset, data_length - offset, body, body_capacity, stored_body_length);
    }
}

inline bool restServerRequestComplete(const RestServerReceiveState& state) {
    if (!state.headers_complete) return false;
    return state.received_body_length >= state.expected_body_length;
}

inline void restServerParseFormBody(const char* body, int body_length,
                                    RestServerRequestField* args, int max_args, int& arg_count) {
    restServerParseParameterString(body, body_length, args, max_args, arg_count);
}