#pragma once

/**
 * @file xtp_timing.h
 * @brief Microsecond-precision timing telemetry for performance monitoring
 * 
 * Enable by defining XTP_TIMING_TELEMETRY before including xtp-lib.h
 * 
 * Features:
 * - Tracks min/max/avg/count for each timed section
 * - Low overhead when disabled (compiles to nothing)
 * - JSON endpoint at /api/timing
 * - Reset via /api/timing/reset
 * 
 * Usage:
 *   XTP_TIMING_START(SECTION_NAME);
 *   // ... code to measure ...
 *   XTP_TIMING_END(SECTION_NAME);
 */

#include <Arduino.h>

// ============================================================================
// Timing Section IDs
// ============================================================================

enum XtpTimingSection {
    XTP_TIME_LOOP_TOTAL = 0,      // Total main loop time
    XTP_TIME_I2C_LOOP,            // i2c_loop()
    XTP_TIME_OLED_UPDATE,         // oled_state_machine_update()
    XTP_TIME_ETH_UPDATE,          // ethernet_state_machine_update()
    XTP_TIME_ETH_LOOP,            // ethernet_loop() total
    XTP_TIME_HTTP_HANDLE,         // rest.handleClient() - total
    XTP_TIME_OTA_LOOP,            // ota_loop()
    XTP_TIME_OLED_TICKER,         // oled_ticker()
    XTP_TIME_HTTP_RECEIVE,        // HTTP request receive phase
    XTP_TIME_HTTP_HANDLER,        // HTTP handler execution
    XTP_TIME_HTTP_SEND,           // HTTP response send phase
    XTP_TIME_SOCKET_CLEANUP,      // Socket health check
    XTP_TIME_SOCKET_CACHE,        // Socket status cache update
    XTP_TIME_I2C_RECOVERY,        // I2C bus recovery
    XTP_TIME_SPI_SELECT,          // SPI device selection
    XTP_TIME_COUNT                // Must be last - total count
};

// Section names for JSON output
static const char* const XTP_TIMING_NAMES[] = {
    "loop_total",
    "i2c_loop",
    "oled_update",
    "eth_state_machine",
    "eth_loop",
    "http_handle",
    "ota_loop",
    "oled_ticker",
    "http_receive",
    "http_handler",
    "http_send",
    "socket_cleanup",
    "socket_cache",
    "i2c_recovery",
    "spi_select"
};

// ============================================================================
// Timing Statistics Structure
// ============================================================================

struct XtpTimingStats {
    uint32_t count = 0;
    uint32_t min_us = UINT32_MAX;
    uint32_t max_us = 0;
    uint64_t total_us = 0;
    uint32_t last_us = 0;
    
    void record(uint32_t us) {
        count++;
        last_us = us;
        total_us += us;
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
    }
    
    void reset() {
        count = 0;
        min_us = UINT32_MAX;
        max_us = 0;
        total_us = 0;
        last_us = 0;
    }
    
    uint32_t avg_us() const {
        return count > 0 ? (uint32_t)(total_us / count) : 0;
    }
};

// ============================================================================
// Global Timing Data
// ============================================================================

#ifdef XTP_TIMING_TELEMETRY

static XtpTimingStats _xtp_timing[XTP_TIME_COUNT];
static uint32_t _xtp_timing_start[XTP_TIME_COUNT] = {0};
static uint32_t _xtp_timing_uptime_start = 0;
static bool _xtp_timing_initialized = false;

// Initialize timing system
inline void xtp_timing_init() {
    if (_xtp_timing_initialized) return;
    _xtp_timing_initialized = true;
    _xtp_timing_uptime_start = millis();
    for (int i = 0; i < XTP_TIME_COUNT; i++) {
        _xtp_timing[i].reset();
    }
}

// Start timing a section
inline void xtp_timing_start(XtpTimingSection section) {
    _xtp_timing_start[section] = micros();
}

// End timing a section and record
inline void xtp_timing_end(XtpTimingSection section) {
    uint32_t elapsed = micros() - _xtp_timing_start[section];
    _xtp_timing[section].record(elapsed);
}

// End timing and return the elapsed time
inline uint32_t xtp_timing_end_get(XtpTimingSection section) {
    uint32_t elapsed = micros() - _xtp_timing_start[section];
    _xtp_timing[section].record(elapsed);
    return elapsed;
}

// Reset all timing stats
inline void xtp_timing_reset() {
    _xtp_timing_uptime_start = millis();
    for (int i = 0; i < XTP_TIME_COUNT; i++) {
        _xtp_timing[i].reset();
    }
}

// Get timing stats for a section
inline const XtpTimingStats& xtp_timing_get(XtpTimingSection section) {
    return _xtp_timing[section];
}

// Generate JSON output
inline void xtp_timing_json(char* buffer, size_t bufferSize) {
    xtp_timing_init();
    
    uint32_t uptime_s = (millis() - _xtp_timing_uptime_start) / 1000;
    
    int offset = snprintf(buffer, bufferSize,
        "{\"uptime_s\":%lu,\"sections\":{", uptime_s);
    
    bool first = true;
    for (int i = 0; i < XTP_TIME_COUNT && offset < (int)bufferSize - 100; i++) {
        const XtpTimingStats& s = _xtp_timing[i];
        if (s.count == 0) continue;  // Skip sections with no data
        
        offset += snprintf(buffer + offset, bufferSize - offset,
            "%s\"%s\":{\"cnt\":%lu,\"min\":%lu,\"max\":%lu,\"avg\":%lu,\"last\":%lu}",
            first ? "" : ",",
            XTP_TIMING_NAMES[i],
            s.count,
            s.min_us == UINT32_MAX ? 0 : s.min_us,
            s.max_us,
            s.avg_us(),
            s.last_us);
        first = false;
    }
    
    snprintf(buffer + offset, bufferSize - offset, "}}");
}

// Macros for convenient usage
#define XTP_TIMING_INIT() xtp_timing_init()
#define XTP_TIMING_START(section) xtp_timing_start(section)
#define XTP_TIMING_END(section) xtp_timing_end(section)
#define XTP_TIMING_END_GET(section) xtp_timing_end_get(section)
#define XTP_TIMING_RESET() xtp_timing_reset()

#else  // XTP_TIMING_TELEMETRY not defined

// No-op macros when telemetry is disabled
#define XTP_TIMING_INIT() ((void)0)
#define XTP_TIMING_START(section) ((void)0)
#define XTP_TIMING_END(section) ((void)0)
#define XTP_TIMING_END_GET(section) (0)
#define XTP_TIMING_RESET() ((void)0)

inline void xtp_timing_init() {}
inline void xtp_timing_reset() {}
inline void xtp_timing_json(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "{\"enabled\":false}");
}

#endif  // XTP_TIMING_TELEMETRY
