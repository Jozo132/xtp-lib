#pragma once

#include <Arduino.h>
#include "xtp_ssd1306.h"  // Our non-blocking SSD1306 driver (replaces ssd1306.h)

#define OLED_COLS (128 / 6)
#define OLED_ROWS (64 / 8)
#define OLED_CHARS (OLED_COLS * OLED_ROWS)

// OLED I2C address (common addresses: 0x3C or 0x3D)
#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3C
#endif

// How often to check if OLED reconnected (ms)
#ifndef OLED_PRESENCE_CHECK_INTERVAL_MS
#define OLED_PRESENCE_CHECK_INTERVAL_MS 2000
#endif

// Max time to spend on OLED operations per loop iteration (us)
#ifndef OLED_MAX_UPDATE_TIME_US
#define OLED_MAX_UPDATE_TIME_US 2000
#endif

// ============================================================================
// OLED State Machine
// ============================================================================

enum OLEDState {
    OLED_STATE_NOT_INITIALIZED,
    OLED_STATE_INITIALIZING,
    OLED_STATE_READY,
    OLED_STATE_UPDATING,
    OLED_STATE_DISCONNECTED,
    OLED_STATE_ERROR
};

struct OLEDStateMachine {
    OLEDState state = OLED_STATE_NOT_INITIALIZED;
    uint32_t stateEnteredAt = 0;
    uint32_t lastPresenceCheck = 0;
    uint32_t lastSuccessfulWrite = 0;
    uint32_t lastHealthCheck = 0;        // Periodic health verification
    uint32_t errorCount = 0;
    uint32_t reconnectCount = 0;
    uint32_t slowWriteCount = 0;         // Track slow I2C operations
    bool present = false;
    bool needsFullRedraw = true;
    int updatePosition = 0;  // For incremental updates
    
    void enterState(OLEDState newState) {
        if (state != newState) {
            state = newState;
            stateEnteredAt = millis();
        }
    }
    
    uint32_t timeInState() const {
        return millis() - stateEnteredAt;
    }
    
    bool isReady() const {
        return state == OLED_STATE_READY || state == OLED_STATE_UPDATING;
    }
    
    const char* getStateName() const {
        switch (state) {
            case OLED_STATE_NOT_INITIALIZED: return "NOT_INIT";
            case OLED_STATE_INITIALIZING: return "INIT";
            case OLED_STATE_READY: return "READY";
            case OLED_STATE_UPDATING: return "UPDATING";
            case OLED_STATE_DISCONNECTED: return "DISCONNECTED";
            case OLED_STATE_ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};

OLEDStateMachine oledState;

char _oled_data_active[OLED_CHARS + 1] = { 0 };
char _oled_data_new[OLED_CHARS + 1] = { 0 };
char _oled_toDraw[OLED_CHARS + 1] = { 0 };

bool oled_initialized = false;
bool oled_force_redraw = false;

// Callback for when OLED reconnects (to refresh display content)
typedef void (*oled_reconnect_callback_t)();
oled_reconnect_callback_t oled_on_reconnect = nullptr;

void oled_set_reconnect_callback(oled_reconnect_callback_t callback) {
    oled_on_reconnect = callback;
}

// I2C device handle for OLED
I2CDevice* oled_i2c_device = nullptr;

// ============================================================================
// OLED Presence Detection (non-blocking)
// ============================================================================

// Threshold for detecting slow I2C (indicates device issue)
#define OLED_SLOW_I2C_THRESHOLD_US  5000   // 5ms is way too long for simple I2C ops
#define OLED_HEALTH_CHECK_INTERVAL_MS 2000 // Check health every 2s when ready

// Force a fresh I2C presence check (uses our SSD1306 driver)
bool oled_check_presence_fresh() {
    return xtp_ssd1306_present();  // Uses i2c_device_present internally
}

// Check cached presence (fast, no I2C traffic)
bool oled_check_presence_cached() {
    return xtp_ssd1306_isPresent();  // Uses cached device state
}

// Legacy function - now does fresh check
bool oled_check_presence() {
    return oled_check_presence_fresh();
}

// ============================================================================
// OLED Setup (non-blocking initial check)
// ============================================================================

void oled_setup() {
    if (oled_initialized) return;
    oled_initialized = true;
    
#ifndef DISABLE_OLED
    // Try to initialize the display (xtp_ssd1306_init handles device registration)
    oledState.present = xtp_ssd1306_init(OLED_I2C_ADDRESS);
    oled_i2c_device = xtp_ssd1306_getDevice();  // Get the device handle from our driver
    
    if (oledState.present) {
        oledState.enterState(OLED_STATE_INITIALIZING);
        xtp_ssd1306_clear();
        
        oledState.needsFullRedraw = true;
        oledState.lastSuccessfulWrite = millis();
        oledState.lastHealthCheck = millis();
        oledState.enterState(OLED_STATE_READY);
    } else {
        Serial.println("[OLED] Display not detected at startup");
        oledState.enterState(OLED_STATE_DISCONNECTED);
    }
#endif
}

// ============================================================================
// OLED State Machine Update (call from main loop)
// ============================================================================

void oled_state_machine_update() {
#ifndef DISABLE_OLED
    if (!oled_initialized) return;
    
    uint32_t now = millis();
    
    switch (oledState.state) {
        
        case OLED_STATE_NOT_INITIALIZED:
            // Should not happen if oled_setup() was called
            break;
            
        case OLED_STATE_INITIALIZING:
            // Initialization is done in setup, transition to ready
            oledState.enterState(OLED_STATE_READY);
            break;
            
        case OLED_STATE_READY:
            // Periodic health check - actively verify device presence
            if (now - oledState.lastHealthCheck >= OLED_HEALTH_CHECK_INTERVAL_MS) {
                oledState.lastHealthCheck = now;
                
                // Do a fresh I2C probe
                if (!oled_check_presence_fresh()) {
                    Serial.println("[OLED] Display disconnected (health check)");
                    oledState.present = false;
                    oledState.errorCount++;
                    oledState.enterState(OLED_STATE_DISCONNECTED);
                    break;
                }
            }
            
            // Check if there's anything to update
            if (oled_force_redraw) {
                oled_force_redraw = false;
                oledState.needsFullRedraw = true;
                oledState.updatePosition = 0;
            }
            
            // Check for pending updates
            {
                bool hasChanges = oledState.needsFullRedraw;
                if (!hasChanges) {
                    for (int i = 0; i < OLED_CHARS; i++) {
                        if (_oled_data_active[i] != _oled_data_new[i]) {
                            hasChanges = true;
                            break;
                        }
                    }
                }
                
                if (hasChanges) {
                    oledState.updatePosition = 0;
                    oledState.enterState(OLED_STATE_UPDATING);
                }
            }
            break;
            
        case OLED_STATE_UPDATING:
            // Perform incremental update with time budget
            {
                uint32_t startTime = micros();
                
                if (oledState.needsFullRedraw) {
                    // Mark all characters as needing redraw
                    for (int i = 0; i < OLED_CHARS; i++) {
                        _oled_data_active[i] = '~';
                    }
                    oledState.needsFullRedraw = false;
                }
                
                // Update characters within time budget
                int updated = 0;
                bool slowDetected = false;
                
                while (oledState.updatePosition < OLED_CHARS) {
                    // Check time budget
                    if (micros() - startTime > OLED_MAX_UPDATE_TIME_US) {
                        break; // Continue next iteration
                    }
                    
                    int i = oledState.updatePosition;
                    char a = _oled_data_active[i];
                    char b = _oled_data_new[i];
                    
                    if (a != b) {
                        _oled_data_active[i] = b;
                        int x = i % OLED_COLS;
                        int y = i / OLED_COLS;
                        
                        // Find consecutive characters to update
                        int j = i;
                        int unchanged = 0;
                        while (j < OLED_CHARS) {
                            if (_oled_data_active[j] == _oled_data_new[j]) unchanged++;
                            else unchanged = 0;
                            if (unchanged >= 2) break;
                            
                            int new_y = j / OLED_COLS;
                            if (new_y != y) break;
                            
                            _oled_toDraw[j - i] = _oled_data_new[j];
                            _oled_data_active[j] = _oled_data_new[j];
                            j++;
                        }
                        _oled_toDraw[j - i] = 0;
                        
                        // Write to display using our non-blocking driver
                        xtp_ssd1306_setCursor(x, y);  // Character coordinates
                        bool writeOk = xtp_ssd1306_print(_oled_toDraw);
                        
                        // Check the write time from our driver
                        uint32_t writeTime = xtp_ssd1306_getLastWriteTime();
                        
                        if (!writeOk) {
                            // Write failed - device disconnected
                            Serial.println("[OLED] Write failed - disconnected");
                            oledState.present = false;
                            oledState.errorCount++;
                            oledState.enterState(OLED_STATE_DISCONNECTED);
                            break;
                        } else if (writeTime > OLED_SLOW_I2C_THRESHOLD_US) {
                            // Write was slow - might be an issue
                            oledState.slowWriteCount++;
                            slowDetected = true;
                        } else {
                            // Normal successful write
                            oledState.lastSuccessfulWrite = now;
                        }
                        
                        updated++;
                        oledState.updatePosition = j;
                    } else {
                        oledState.updatePosition++;
                    }
                }
                
                // If we detected slow writes, verify the device is still there
                if (slowDetected) {
                    if (!oled_check_presence_fresh()) {
                        Serial.println("[OLED] Display disconnected (slow I2C)");
                        oledState.present = false;
                        oledState.errorCount++;
                        oledState.enterState(OLED_STATE_DISCONNECTED);
                        break;
                    }
                }
                
                // Check if update is complete
                if (oledState.updatePosition >= OLED_CHARS) {
                    oledState.updatePosition = 0;
                    oledState.enterState(OLED_STATE_READY);
                }
            }
            break;
            
        case OLED_STATE_DISCONNECTED:
            // Periodically check if OLED reconnected
            if (now - oledState.lastPresenceCheck >= OLED_PRESENCE_CHECK_INTERVAL_MS) {
                oledState.lastPresenceCheck = now;
                
                // If bus has errors, try recovery first
                if (i2cBus.busError) {
                    Serial.println("[OLED] Attempting I2C bus recovery...");
                    i2c_bus_recovery();
                }
                
                // Now try to detect the display
                if (xtp_ssd1306_present()) {
                    Serial.println("[OLED] Display reconnected - reinitializing...");
                    
                    // Small delay to let the OLED power up properly
                    delay(50);
                    
                    // Full hardware reinitialization using our driver
                    if (xtp_ssd1306_init(OLED_I2C_ADDRESS)) {
                        oledState.present = true;
                        oledState.reconnectCount++;
                        oledState.slowWriteCount = 0;
                        
                        // Clear display
                        xtp_ssd1306_clear();
                        
                        // Reset our character buffers to force full redraw
                        memset(_oled_data_active, ' ', OLED_CHARS);
                        _oled_data_active[OLED_CHARS] = 0;
                        
                        // Also clear the new buffer so callback can populate it fresh
                        memset(_oled_data_new, ' ', OLED_CHARS);
                        _oled_data_new[OLED_CHARS] = 0;
                        
                        // Call reconnect callback to refresh display content
                        if (oled_on_reconnect) {
                            oled_on_reconnect();
                        }
                        
                        oledState.needsFullRedraw = true;
                        oledState.updatePosition = 0;
                        oledState.lastSuccessfulWrite = now;
                        oledState.lastHealthCheck = now;
                        
                        Serial.println("[OLED] Reinitialization complete");
                        oledState.enterState(OLED_STATE_READY);
                    } else {
                        Serial.println("[OLED] Reinitialization failed");
                    }
                }
            }
            break;
            
        case OLED_STATE_ERROR:
            // Try to recover after a delay
            if (oledState.timeInState() >= 5000) {
                // Only do bus recovery if we have multiple I2C errors
                if (i2cBus.busError) {
                    i2c_bus_recovery();
                }
                oledState.enterState(OLED_STATE_DISCONNECTED);
            }
            break;
    }
    
    // The periodic health check in OLED_STATE_READY and slow I2C detection in
    // OLED_STATE_UPDATING handle disconnect detection, so no extra watchdog needed
#endif
}

// ============================================================================
// Public API (non-blocking)
// ============================================================================

// Queue a message for display (non-blocking)
void displayMsg(const char* message) {
#ifndef DISABLE_OLED
    if (!oled_initialized) return;
    if (!oledState.isReady() && oledState.state != OLED_STATE_DISCONNECTED) return;
    
    int len = strlen(message);
    for (int i = 0; i < len && i < OLED_CHARS; i++) {
        _oled_data_new[i] = message[i];
    }
    // State machine will handle the actual update
#endif
}

// Queue text at position (non-blocking)
void oled_print(const char* message, int x, int y) {
#ifndef DISABLE_OLED
    if (!oled_initialized) return;
    // Accept prints even when disconnected - they'll be shown when reconnected
    
    int len = strlen(message);
    int index = x + y * OLED_COLS;
    for (int i = 0; i < len; i++) {
        if (index >= OLED_CHARS) break;
        _oled_data_new[index++] = message[i];
    }
    // State machine will handle the actual update
#endif
}

// Legacy synchronous draw - now just triggers state machine
void oled_draw() {
#ifndef DISABLE_OLED
    if (!oled_initialized) return;
    
    // For backwards compatibility, run the state machine once
    oled_state_machine_update();
#endif
}

// Spinner/ticker (non-blocking)
int xtp_spinner_index = 0;
int xtp_spinner_tick = 0;

void oled_ticker() {
#ifndef DISABLE_OLED
#ifdef XTP_DISPLAY_TICK
    if (!oled_initialized) return;
    if (!oledState.isReady()) return;
    
    constexpr int spinner_count = 8;
    constexpr int spinner_frame_duration_ms = 200;
    
    if (xtp_spinner_tick >= spinner_count * spinner_frame_duration_ms) 
        xtp_spinner_tick = 0;
    xtp_spinner_tick++;
    
    int spinner_index = (xtp_spinner_tick % (spinner_count * spinner_frame_duration_ms)) / spinner_frame_duration_ms;
    
    if (spinner_index != xtp_spinner_index) {
        xtp_spinner_index = spinner_index;
        
        const uint8_t sprite[][6] = {
          { 0b00000100, 0b00001010, 0b00010000, 0b10100000, 0b01000000, 0b00000000 },
          { 0b00001100, 0b00010000, 0b00010000, 0b00010000, 0b01100000, 0b00000000 },
          { 0b00011000, 0b00010000, 0b00010000, 0b00010000, 0b00110000, 0b00000000 },
          { 0b00110000, 0b00010000, 0b00010000, 0b00010000, 0b00011000, 0b00000000 },
          { 0b01100000, 0b00100000, 0b00010000, 0b00001000, 0b00001100, 0b00000000 },
          { 0b01000000, 0b01000000, 0b00111000, 0b00000100, 0b00000100, 0b00000000 },
          { 0b00000000, 0b01000000, 0b01111100, 0b00000100, 0b00000000, 0b00000000 },
          { 0b00000000, 0b00000100, 0b01111100, 0b01000000, 0b00000000, 0b00000000 }
        };
        
        const uint8_t* bitmap = &sprite[xtp_spinner_index][0];
        xtp_ssd1306_drawBuffer(0, 7, 6, 8, bitmap);  // Page 7 = bottom row
    }
#endif // XTP_DISPLAY_TICK
#endif // DISABLE_OLED
}

// ============================================================================
// Status API
// ============================================================================

bool oled_is_connected() { return oledState.present; }
bool oled_is_ready() { return oledState.isReady(); }
const char* oled_state_name() { return oledState.getStateName(); }
uint32_t oled_error_count() { return oledState.errorCount; }
uint32_t oled_reconnect_count() { return oledState.reconnectCount; }
uint32_t oled_slow_write_count() { return oledState.slowWriteCount; }

// Get OLED status as JSON
void oled_status_json(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize,
        "{\"state\":\"%s\",\"present\":%s,\"ready\":%s,\"errors\":%lu,\"reconnects\":%lu,\"slowWrites\":%lu}",
        oledState.getStateName(),
        oledState.present ? "true" : "false",
        oledState.isReady() ? "true" : "false",
        oledState.errorCount,
        oledState.reconnectCount,
        oledState.slowWriteCount
    );
}