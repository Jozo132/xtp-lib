#pragma once

/**
 * @file xtp_i2c.h
 * @brief Non-blocking I2C Bus Manager with Multi-Device Support
 * 
 * This module provides a robust I2C bus management system that:
 * - Tracks multiple I2C devices independently
 * - Prevents bus blocking when devices are disconnected
 * - Automatically throttles retry attempts for missing devices
 * - Supports hot-plug (connect/disconnect devices at runtime)
 * - Provides bus recovery for stuck I2C lines
 * 
 * ============================================================================
 * QUICK START - Adding Your Own I2C Device
 * ============================================================================
 * 
 * 1. REGISTER YOUR DEVICE (in setup):
 *    ```cpp
 *    #define MY_SENSOR_ADDR 0x48
 *    I2CDevice* mySensor = nullptr;
 *    
 *    void setup() {
 *        // ... other setup ...
 *        mySensor = i2cBus.registerDevice(MY_SENSOR_ADDR, "TempSensor", false);
 *    }
 *    ```
 * 
 * 2. CHECK BEFORE ACCESSING (non-blocking pattern):
 *    ```cpp
 *    void loop() {
 *        // Only access if device is present or should be retried
 *        if (mySensor->shouldRetry()) {
 *            // Try to read/write - functions return false if device not responding
 *            uint8_t data[2];
 *            if (i2c_read_reg(MY_SENSOR_ADDR, 0x00, data, 2) > 0) {
 *                // Success! Process data
 *                int16_t temp = (data[0] << 8) | data[1];
 *            }
 *            // If failed, device state is updated automatically
 *            // Next shouldRetry() will return false for I2C_RECOVERY_INTERVAL_MS
 *        }
 *    }
 *    ```
 * 
 * 3. OR USE THE HELPER MACRO (for simple devices):
 *    ```cpp
 *    I2C_DEVICE_DECLARE(fram, 0x50);  // Creates fram_i2c_dev, fram_i2c_init(), etc.
 *    
 *    void setup() {
 *        fram_i2c_init();  // Register with bus manager
 *    }
 *    
 *    void loop() {
 *        if (fram_is_present()) {
 *            // Device is responding
 *        }
 *    }
 *    ```
 * 
 * ============================================================================
 * AVAILABLE I2C HELPER FUNCTIONS
 * ============================================================================
 * 
 * All functions automatically update device state and respect retry throttling:
 * 
 *   bool i2c_device_present(addr)           - Check if device responds
 *   bool i2c_write(addr, data, len)         - Write raw bytes
 *   bool i2c_write_byte(addr, byte)         - Write single byte
 *   bool i2c_write_reg(addr, reg, data)     - Write to register
 *   bool i2c_write_reg_buf(addr, reg, data, len) - Write buffer to register
 *   size_t i2c_read(addr, buf, len)         - Read raw bytes (returns count)
 *   size_t i2c_read_reg(addr, reg, buf, len) - Read from register
 * 
 * ============================================================================
 * I2CDEVICE STRUCT METHODS
 * ============================================================================
 * 
 *   dev->isPresent()    - Returns true if device was last seen responding
 *   dev->shouldRetry()  - Returns true if OK to attempt I2C transaction
 *                         (either device is present, or retry interval elapsed)
 *   dev->state          - Current state: I2C_DEV_UNKNOWN, I2C_DEV_PRESENT,
 *                                        I2C_DEV_NOT_PRESENT, I2C_DEV_ERROR
 *   dev->errorCount     - Number of errors recorded for this device
 * 
 * ============================================================================
 * BUS RECOVERY
 * ============================================================================
 * 
 * If the I2C bus gets stuck (SDA held low), call:
 *   i2c_bus_recovery();
 * 
 * This toggles SCL to release stuck slaves and reinitializes the bus.
 * The OLED module does this automatically when it detects errors.
 * 
 * ============================================================================
 * CONFIGURATION (define before including)
 * ============================================================================
 * 
 *   #define I2C_TIMEOUT_MS 10              - Transaction timeout (ms)
 *   #define I2C_RECOVERY_INTERVAL_MS 2000  - Retry interval for failed devices
 *   #define I2C_MAX_DEVICES 8              - Maximum tracked devices
 * 
 * ============================================================================
 * MONITORING
 * ============================================================================
 * 
 * REST API endpoint: GET /api/i2c-status
 * Returns JSON with bus state and all registered devices.
 * 
 * Serial debugging:
 *   i2c_scan_print();  - Scan bus and print found devices
 * 
 */

#include "xtp_uart.h"

#include <Wire.h>

// ============================================================================
// I2C Configuration
// ============================================================================

#ifndef I2C_TIMEOUT_MS
#define I2C_TIMEOUT_MS 10  // Max time to wait for I2C transaction
#endif

#ifndef I2C_RECOVERY_INTERVAL_MS
#define I2C_RECOVERY_INTERVAL_MS 2000  // How often to retry failed devices
#endif

#ifndef I2C_MAX_DEVICES
#define I2C_MAX_DEVICES 8  // Maximum tracked devices
#endif

// ============================================================================
// I2C Device State Structure
// ============================================================================

enum I2CDeviceState {
    I2C_DEV_UNKNOWN,       // Never checked
    I2C_DEV_PRESENT,       // Device responding
    I2C_DEV_NOT_PRESENT,   // Device not found (NACK)
    I2C_DEV_ERROR          // Bus error when accessing
};

struct I2CDevice {
    uint8_t address = 0;
    const char* name = nullptr;
    I2CDeviceState state = I2C_DEV_UNKNOWN;
    uint32_t lastCheckTime = 0;
    uint32_t lastSuccessTime = 0;
    uint32_t successCount = 0;
    uint32_t errorCount = 0;
    bool required = false;  // If true, bus recovery attempted on failure
    
    bool isPresent() const { return state == I2C_DEV_PRESENT; }
    bool shouldRetry() const {
        if (state == I2C_DEV_PRESENT) return true;
        return (millis() - lastCheckTime) >= I2C_RECOVERY_INTERVAL_MS;
    }
    
    void recordSuccess() {
        state = I2C_DEV_PRESENT;
        lastSuccessTime = millis();
        successCount++;
    }
    
    void recordNotPresent() {
        state = I2C_DEV_NOT_PRESENT;
        lastCheckTime = millis();
    }
    
    void recordError() {
        state = I2C_DEV_ERROR;
        lastCheckTime = millis();
        errorCount++;
    }
};

// ============================================================================
// I2C Bus Manager
// ============================================================================

struct I2CBusManager {
    bool initialized = false;
    bool busError = false;
    uint32_t lastBusError = 0;
    uint32_t busErrorCount = 0;
    uint32_t totalTransactions = 0;
    
    I2CDevice devices[I2C_MAX_DEVICES];
    uint8_t deviceCount = 0;
    
    // Register a device to track
    I2CDevice* registerDevice(uint8_t address, const char* name, bool required = false) {
        // Check if already registered
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].address == address) {
                return &devices[i];
            }
        }
        
        // Add new device
        if (deviceCount >= I2C_MAX_DEVICES) {
            Serial.println("[I2C] ERROR: Max devices reached");
            return nullptr;
        }
        
        I2CDevice* dev = &devices[deviceCount++];
        dev->address = address;
        dev->name = name;
        dev->required = required;
        dev->state = I2C_DEV_UNKNOWN;
        
        Serial.printf("[I2C] Registered device '%s' at 0x%02X\n", name, address);
        return dev;
    }
    
    // Find a registered device by address
    I2CDevice* findDevice(uint8_t address) {
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].address == address) {
                return &devices[i];
            }
        }
        return nullptr;
    }
    
    // Record a successful transaction
    void recordSuccess(uint8_t address) {
        totalTransactions++;
        busError = false;
        I2CDevice* dev = findDevice(address);
        if (dev) dev->recordSuccess();
    }
    
    // Record a bus error
    void recordBusError(uint8_t address) {
        busError = true;
        lastBusError = millis();
        busErrorCount++;
        I2CDevice* dev = findDevice(address);
        if (dev) dev->recordError();
    }
    
    // Record device not present (NACK - bus is OK)
    void recordNotPresent(uint8_t address) {
        totalTransactions++;
        I2CDevice* dev = findDevice(address);
        if (dev) dev->recordNotPresent();
    }
};

I2CBusManager i2cBus;

bool i2c_initialized = false;

// ============================================================================
// I2C Bus Recovery
// ============================================================================

// I2C bus recovery - toggle SCL to release stuck SDA
void i2c_bus_recovery() {
    Serial.println("[I2C] Attempting bus recovery");
    
    // End current Wire session
    Wire.end();
    
    // Manually toggle SCL to release any stuck slave
    pinMode(I2C_SCL_pin, OUTPUT);
    pinMode(I2C_SDA_pin, INPUT_PULLUP);
    
    for (int i = 0; i < 16; i++) {
        digitalWrite(I2C_SCL_pin, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL_pin, HIGH);
        delayMicroseconds(5);
        
        // Check if SDA is released
        if (digitalRead(I2C_SDA_pin) == HIGH) {
            break;
        }
    }
    
    // Generate STOP condition
    pinMode(I2C_SDA_pin, OUTPUT);
    digitalWrite(I2C_SDA_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL_pin, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA_pin, HIGH);
    delayMicroseconds(5);
    
    // Reinitialize I2C
    Wire.setSDA(I2C_SDA_pin);
    Wire.setSCL(I2C_SCL_pin);
    Wire.setClock(I2C_CLOCK);
    Wire.begin();
    
    i2cBus.busError = false;
    Serial.println("[I2C] Bus recovery complete");
}

// ============================================================================
// I2C Setup
// ============================================================================

void i2c_setup() {
    if (i2c_initialized) return;
    i2c_initialized = true;
    
    Wire.setSDA(I2C_SDA_pin);
    Wire.setSCL(I2C_SCL_pin);
    Wire.setClock(I2C_CLOCK);
    Wire.begin();
    
    // Set Wire timeout (if supported by the platform)
#if defined(ARDUINO_ARCH_STM32)
    Wire.setTimeout(I2C_TIMEOUT_MS); // timeout in milliseconds
#endif
    
    i2cBus.initialized = true;
    Serial.println("[I2C] Bus initialized");
}

// ============================================================================
// Core I2C Transaction Helpers (with error tracking)
// ============================================================================

// Check if a device is present (updates device state)
// Returns: 0 = present, 1 = not present, 2+ = bus error
uint8_t i2c_check_device(uint8_t address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        i2cBus.recordSuccess(address);
        return 0;
    } else if (error == 2) {
        // NACK on address - device not present but bus OK
        i2cBus.recordNotPresent(address);
        return 1;
    } else {
        // Bus error (timeout, arbitration lost, etc.)
        i2cBus.recordBusError(address);
        return error;
    }
}

// Check if device is present (simple bool version)
bool i2c_device_present(uint8_t address) {
    I2CDevice* dev = i2cBus.findDevice(address);
    if (dev && !dev->shouldRetry()) {
        return dev->isPresent();
    }
    return i2c_check_device(address) == 0;
}

// Write data to device (with error tracking)
// Returns: true on success
bool i2c_write(uint8_t address, const uint8_t* data, size_t length) {
    I2CDevice* dev = i2cBus.findDevice(address);
    if (dev && !dev->shouldRetry()) {
        return false;
    }
    
    Wire.beginTransmission(address);
    Wire.write(data, length);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        i2cBus.recordSuccess(address);
        return true;
    } else if (error == 2) {
        i2cBus.recordNotPresent(address);
        return false;
    } else {
        i2cBus.recordBusError(address);
        return false;
    }
}

// Write single byte to device
bool i2c_write_byte(uint8_t address, uint8_t data) {
    return i2c_write(address, &data, 1);
}

// Write register + data
bool i2c_write_reg(uint8_t address, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg, data };
    return i2c_write(address, buf, 2);
}

// Write register + multi-byte data
bool i2c_write_reg_buf(uint8_t address, uint8_t reg, const uint8_t* data, size_t length) {
    I2CDevice* dev = i2cBus.findDevice(address);
    if (dev && !dev->shouldRetry()) {
        return false;
    }
    
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, length);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        i2cBus.recordSuccess(address);
        return true;
    } else if (error == 2) {
        i2cBus.recordNotPresent(address);
        return false;
    } else {
        i2cBus.recordBusError(address);
        return false;
    }
}

// Read data from device (with error tracking)
// Returns: number of bytes read, 0 on error
size_t i2c_read(uint8_t address, uint8_t* buffer, size_t length) {
    I2CDevice* dev = i2cBus.findDevice(address);
    if (dev && !dev->shouldRetry()) {
        return 0;
    }
    
    size_t received = Wire.requestFrom(address, length);
    
    if (received == 0) {
        i2cBus.recordNotPresent(address);
        return 0;
    }
    
    size_t i = 0;
    while (Wire.available() && i < length) {
        buffer[i++] = Wire.read();
    }
    
    i2cBus.recordSuccess(address);
    return i;
}

// Read from register
size_t i2c_read_reg(uint8_t address, uint8_t reg, uint8_t* buffer, size_t length) {
    // First write the register address
    Wire.beginTransmission(address);
    Wire.write(reg);
    uint8_t error = Wire.endTransmission(false); // Keep bus active
    
    if (error != 0) {
        if (error == 2) {
            i2cBus.recordNotPresent(address);
        } else {
            i2cBus.recordBusError(address);
        }
        return 0;
    }
    
    // Then read the data
    return i2c_read(address, buffer, length);
}

// ============================================================================
// I2C Scanning
// ============================================================================

struct i2c_scan_result_t {
    uint8_t addresses[128];
    uint8_t count;
};

i2c_scan_result_t i2c_scan() {
    i2c_scan_result_t result;
    result.count = 0;
    
    for (uint8_t address = 1; address < 127; address++) {
        if (i2c_check_device(address) == 0) {
            result.addresses[result.count++] = address;
        }
    }
    return result;
}

void i2c_scan_print() {
    i2c_scan_result_t result = i2c_scan();
    Serial.println("[I2C] Scan results:");
    Serial.printf("  Found %d device(s)\n", result.count);
    for (uint8_t i = 0; i < result.count; i++) {
        uint8_t addr = result.addresses[i];
        I2CDevice* dev = i2cBus.findDevice(addr);
        if (dev && dev->name) {
            Serial.printf("  - 0x%02X: %s\n", addr, dev->name);
        } else {
            Serial.printf("  - 0x%02X: (unknown)\n", addr);
        }
    }
}

// ============================================================================
// I2C Status API
// ============================================================================

bool i2c_has_error() { return i2cBus.busError; }
uint32_t i2c_error_count() { return i2cBus.busErrorCount; }
uint32_t i2c_transaction_count() { return i2cBus.totalTransactions; }

// Check all registered devices for presence
void i2c_check_all_devices() {
    for (uint8_t i = 0; i < i2cBus.deviceCount; i++) {
        I2CDevice* dev = &i2cBus.devices[i];
        if (dev->shouldRetry()) {
            i2c_check_device(dev->address);
        }
    }
}

// Get I2C bus status as JSON
void i2c_status_json(char* buffer, size_t bufferSize) {
    int offset = snprintf(buffer, bufferSize,
        "{\"initialized\":%s,\"busError\":%s,\"errorCount\":%lu,\"transactions\":%lu,\"devices\":[",
        i2cBus.initialized ? "true" : "false",
        i2cBus.busError ? "true" : "false",
        i2cBus.busErrorCount,
        i2cBus.totalTransactions);
    
    for (uint8_t i = 0; i < i2cBus.deviceCount && offset < (int)bufferSize - 50; i++) {
        I2CDevice* dev = &i2cBus.devices[i];
        const char* stateStr = "unknown";
        switch (dev->state) {
            case I2C_DEV_PRESENT: stateStr = "present"; break;
            case I2C_DEV_NOT_PRESENT: stateStr = "not_present"; break;
            case I2C_DEV_ERROR: stateStr = "error"; break;
            default: stateStr = "unknown"; break;
        }
        
        offset += snprintf(buffer + offset, bufferSize - offset,
            "%s{\"addr\":\"0x%02X\",\"name\":\"%s\",\"state\":\"%s\",\"errors\":%lu}",
            i > 0 ? "," : "",
            dev->address,
            dev->name ? dev->name : "?",
            stateStr,
            dev->errorCount);
    }
    
    snprintf(buffer + offset, bufferSize - offset, "]}");
}

// ============================================================================
// Convenience Macros for User Devices
// ============================================================================

// Helper to create a non-blocking device wrapper
#define I2C_DEVICE_DECLARE(name, addr) \
    I2CDevice* name##_i2c_dev = nullptr; \
    bool name##_i2c_init() { \
        name##_i2c_dev = i2cBus.registerDevice(addr, #name, false); \
        return name##_i2c_dev != nullptr; \
    } \
    bool name##_is_present() { \
        return name##_i2c_dev && name##_i2c_dev->isPresent(); \
    } \
    bool name##_should_retry() { \
        return name##_i2c_dev && name##_i2c_dev->shouldRetry(); \
    }
