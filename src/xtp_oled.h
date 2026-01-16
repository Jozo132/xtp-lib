#pragma once

#include <Arduino.h>
#include <ssd1306.h>
#include "xtp_i2c.h"

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
    uint32_t errorCount = 0;
    uint32_t reconnectCount = 0;
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

// I2C device handle for OLED
I2CDevice* oled_i2c_device = nullptr;

// ============================================================================
// OLED Presence Detection (non-blocking)
// ============================================================================

bool oled_check_presence() {
    if (oled_i2c_device) {
        return oled_i2c_device->isPresent() || 
               (oled_i2c_device->shouldRetry() && i2c_device_present(OLED_I2C_ADDRESS));
    }
    return i2c_device_present(OLED_I2C_ADDRESS);
}

// ============================================================================
// OLED Setup (non-blocking initial check)
// ============================================================================

void oled_setup() {
    if (oled_initialized) return;
    oled_initialized = true;
    
#ifndef DISABLE_OLED
    // Register OLED with I2C bus manager
    oled_i2c_device = i2cBus.registerDevice(OLED_I2C_ADDRESS, "OLED", false);
    
    // Check if OLED is present
    oledState.present = oled_check_presence();
    
    if (oledState.present) {
        Serial.println("[OLED] Display detected, initializing...");
        oledState.enterState(OLED_STATE_INITIALIZING);
        
        ssd1306_setFixedFont(ssd1306xled_font6x8);
        ssd1306_128x64_i2c_init();
        ssd1306_clearScreen();
        
        oledState.needsFullRedraw = true;
        oledState.lastSuccessfulWrite = millis();
        oledState.enterState(OLED_STATE_READY);
        Serial.println("[OLED] Initialization complete");
    } else {
        Serial.println("[OLED] Display not detected");
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
                        
                        // Write to display
                        ssd1306_setCursor(x * 6, y * 8);
                        ssd1306_print(_oled_toDraw);
                        
                        oledState.lastSuccessfulWrite = now;
                        updated++;
                        oledState.updatePosition = j;
                    } else {
                        oledState.updatePosition++;
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
            // Periodically check if OLED reconnected (using I2C device tracking)
            if (now - oledState.lastPresenceCheck >= OLED_PRESENCE_CHECK_INTERVAL_MS) {
                oledState.lastPresenceCheck = now;
                
                // Only check if I2C bus is healthy and device should be retried
                bool shouldCheck = true;
                if (oled_i2c_device) {
                    shouldCheck = oled_i2c_device->shouldRetry() && !i2cBus.busError;
                }
                
                if (shouldCheck && oled_check_presence()) {
                    Serial.println("[OLED] Display reconnected!");
                    oledState.present = true;
                    oledState.reconnectCount++;
                    
                    // Reinitialize
                    ssd1306_setFixedFont(ssd1306xled_font6x8);
                    ssd1306_128x64_i2c_init();
                    ssd1306_clearScreen();
                    
                    oledState.needsFullRedraw = true;
                    oledState.updatePosition = 0;
                    oledState.lastSuccessfulWrite = now;
                    oledState.enterState(OLED_STATE_READY);
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
    
    // Watchdog: if we haven't written successfully in a while, assume disconnected
    if (oledState.state == OLED_STATE_READY || oledState.state == OLED_STATE_UPDATING) {
        if (now - oledState.lastSuccessfulWrite > 10000) {
            // Check presence using I2C device state first (avoid unnecessary I2C traffic)
            bool present = false;
            if (oled_i2c_device && oled_i2c_device->isPresent()) {
                // Device was recently OK, do a fresh check
                present = oled_check_presence();
            }
            
            if (!present) {
                Serial.println("[OLED] Display lost connection");
                oledState.present = false;
                oledState.errorCount++;
                oledState.enterState(OLED_STATE_DISCONNECTED);
            } else {
                oledState.lastSuccessfulWrite = now;
            }
        }
    }
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
        ssd1306_drawBuffer(0, 6, 6, 8, bitmap);
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

// Get OLED status as JSON
void oled_status_json(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize,
        "{\"state\":\"%s\",\"present\":%s,\"ready\":%s,\"errors\":%lu,\"reconnects\":%lu}",
        oledState.getStateName(),
        oledState.present ? "true" : "false",
        oledState.isReady() ? "true" : "false",
        oledState.errorCount,
        oledState.reconnectCount
    );
}