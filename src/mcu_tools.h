#pragma once

#include "stdint.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

#ifdef __arm__
extern "C" char* sbrk(int incr);
#elif defined(ESP8266) || defined(ESP32) || defined(__SIMULATOR__)
// ESP8266 has no sbrk
#else  // __ARM__
extern char* __brkval;
#endif  // __arm__

int freeMemory() {
#ifdef __WASM__
    return heap_size - heap_used;
#elif defined(ESP8266) || defined(ESP32)
    return ESP.getFreeHeap();
#elif defined(__SIMULATOR__)
    return 9000;
#else
    char top;
#ifdef __arm__
    return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
    return &top - __brkval;
#else  // __arm__
    return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
#endif  // ESP8266
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// From PATH sprintf the file name and folder name
void getFileNameFromPath(const char* path, char* filename, int max_len) {
    // From the right find the 3rd instance of '/'
    int len = strlen(path);
    int i = len - 1;
    int count = 0;
    while (i >= 0) {
        if (path[i] == '/') {
            count++;
            if (count == 3) {
                break;
            }
        }
        i--;
    }
    if (count == 3) {
        i++;
        int j = 0;
        while (i < len && j < max_len) {
            filename[j] = path[i];
            i++;
            j++;
        }
        filename[j] = 0;
    } else {
        filename[0] = 0;
    }
}