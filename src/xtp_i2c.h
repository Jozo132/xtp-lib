#pragma once

#include "xtp_uart.h"

#include <Wire.h>

bool i2c_initialized = false;
void i2c_setup() {
    if (i2c_initialized) return;
    i2c_initialized = true;
    Wire.setSDA(I2C_SDA_pin);
    Wire.setSCL(I2C_SCL_pin);
    Wire.setClock(I2C_CLOCK);
    Wire.begin();
}

struct i2c_scan_result_t {
    uint8_t addresses[128];
    uint8_t count;
} i2c_scan_result;

i2c_scan_result_t i2c_scan() {
    i2c_scan_result_t result = i2c_scan_result;
    result.count = 0;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        if (error == 0) {
            result.addresses[result.count] = address;
            result.count++;
        }
    }
    return result;
}


void i2c_scan_print() {
    i2c_scan_result_t result = i2c_scan();
    Serial.println("I2C devices found: " + String(result.count));
    for (uint8_t i = 0; i < result.count; i++) {
        Serial.printf("   - Device at address: 0x%02X\n", result.addresses[i]);
    }
}
