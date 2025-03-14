#pragma once

#include <Arduino.h>
#include "xtp_config.h"

long temp_millis = 0;

class Interval {
private:
    bool enabled = true;
    long last_trigger = 0;
    long duration = 1000;
public:
    Interval(long interval) {
        this->duration = interval;
    }
    bool check() {
        if (enabled) {
            temp_millis = millis();
            if (temp_millis < this->last_trigger || temp_millis >= this->last_trigger + this->duration) {
                this->last_trigger = temp_millis;
                return true;
            }
        }
        return false;
    }
    void reset() {
        this->last_trigger = millis();
    }
    void clear() {
        enabled = false;
    }
    void set() {
        enabled = true;
        this->reset();
    }
    void set(long interval) {
        this->duration = interval;
        enabled = true;
        this->reset();
    }
};


// JavaScript setTimeout alternative
class Timeout {
private:
    bool enabled = true;
    bool triggered = true;
    long last_trigger = 0;
    long duration = 1000;
public:
    Timeout(long timeout) {
        this->duration = timeout;
    }
    bool check() {
        if (enabled && !triggered) {
            temp_millis = millis();
            if (temp_millis < this->last_trigger || temp_millis >= this->last_trigger + this->duration) {
                triggered = true;
                return true;
            }
        }
        return false;
    }
    void reset() {
        this->last_trigger = millis();
        triggered = false;
    }
    void clear() {
        enabled = false;
    }
    void set() {
        enabled = true;
        this->reset();
    }
    void set(long timeout) {
        this->duration = timeout;
        enabled = true;
        this->reset();
    }
};


class DebounceRead {
private:
    bool previous = false;
    int stable_cycles = 0;
    int _cycles = 15;
    int _pin = -1;
public:
    bool output = false;

    DebounceRead(int pin = -1, int cycles = 15) {
        _pin = pin;
        _cycles = cycles;
    }

    bool read() {
        bool input = _pin < 0 ? false : digitalRead(_pin);
        return read(input);
    }

    bool read(bool input) {
        if (input == previous) {
            if (stable_cycles <= _cycles) stable_cycles++;
        } else {
            stable_cycles = 0;
        }
        if (stable_cycles >= _cycles) {
            output = input;
        }
        previous = input;
        return output;
    }
};

Interval P_10s_timer(10000);
Interval P_5s_timer(5000);
Interval P_1s_timer(1000);
Interval P_500ms_timer(500);
Interval P_200ms_timer(200);
Interval P_100ms_timer(100);
Interval P_50ms_timer(50);
Interval P_10ms_timer(10);

bool P_1day = false;
bool P_12hr = false;
bool P_6hr = false;
bool P_5hr = false;
bool P_4hr = false;
bool P_3hr = false;
bool P_2hr = false;
bool P_1hr = false;
bool P_30min = false;
bool P_15min = false;
bool P_10min = false;
bool P_5min = false;
bool P_1min = false;
bool P_30s = false;
bool P_10s = false;
bool P_5s = false;
bool P_1s = false;
bool P_500ms = false;
bool P_200ms = false;
bool P_100ms = false;
bool P_50ms = false;
bool P_10ms = false;

int P_1day_hour_cnt = 0;
int P_12hr_hour_cnt = 0;
int P_6hr_hour_cnt = 0;
int P_3hr_hour_cnt = 0;
int P_2hr_hour_cnt = 0;
int P_1hr_min_cnt = 0;
int P_30min_min_cnt = 0;
int P_15min_min_cnt = 0;
int P_10min_min_cnt = 0;
int P_5min_min_cnt = 0;
int P_1min_sec_cnt = 0;
int P_30s_sec_cnt = 0;


void IntervalGlobalLoopCheck() {
    P_1day = false;
    P_12hr = false;
    P_6hr = false;
    P_5hr = false;
    P_4hr = false;
    P_3hr = false;
    P_2hr = false;
    P_1hr = false;
    P_30min = false;
    P_15min = false;
    P_10min = false;
    P_5min = false;
    P_1min = false;
    P_30s = false;
    P_10s = P_10s_timer.check();
    P_5s = P_5s_timer.check();
    P_1s = P_1s_timer.check();
    P_500ms = P_500ms_timer.check();
    P_200ms = P_200ms_timer.check();
    P_100ms = P_100ms_timer.check();
    P_50ms = P_50ms_timer.check();
    P_10ms = P_10ms_timer.check();
    if (P_1s) {
        P_1min_sec_cnt++;
        P_30s_sec_cnt++;
    }
    if (P_30s_sec_cnt >= 30) {
        P_30s_sec_cnt = 0;
        P_30s = true;
    }
    if (P_1min_sec_cnt >= 60) {
        P_1min_sec_cnt = 0;
        P_1min = true;
    }
    if (P_1min) {
        P_5min_min_cnt++;
        P_10min_min_cnt++;
        P_15min_min_cnt++;
        P_30min_min_cnt++;
        P_1hr_min_cnt++;
    }
    if (P_5min_min_cnt >= 5) {
        P_5min_min_cnt = 0;
        P_5min = true;
    }
    if (P_10min_min_cnt >= 10) {
        P_10min_min_cnt = 0;
        P_10min = true;
    }
    if (P_15min_min_cnt >= 15) {
        P_15min_min_cnt = 0;
        P_15min = true;
    }
    if (P_30min_min_cnt >= 30) {
        P_30min_min_cnt = 0;
        P_30min = true;
    }
    if (P_1hr_min_cnt >= 60) {
        P_1hr_min_cnt = 0;
        P_1hr = true;
    }
    if (P_1hr) {
        P_2hr_hour_cnt++;
        P_3hr_hour_cnt++;
        P_6hr_hour_cnt++;
        P_12hr_hour_cnt++;
        P_1day_hour_cnt++;
    }
    if (P_2hr_hour_cnt >= 2) {
        P_2hr_hour_cnt = 0;
        P_2hr = true;
    }
    if (P_3hr_hour_cnt >= 3) {
        P_3hr_hour_cnt = 0;
        P_3hr = true;
    }
    if (P_6hr_hour_cnt >= 6) {
        P_6hr_hour_cnt = 0;
        P_6hr = true;
    }
    if (P_12hr_hour_cnt >= 12) {
        P_12hr_hour_cnt = 0;
        P_12hr = true;
    }
    if (P_1day_hour_cnt >= 24) {
        P_1day_hour_cnt = 0;
        P_1day = true;
    }
}


bool MCU_UID_loaded = false;
// uint8_t MCU_UID[12] = { 0 };

// uint32_t* uid32ptr_0 = (uint32_t*) (STM32_UID_ADDRESS + 0x00);
// uint32_t* uid32ptr_1 = (uint32_t*) (STM32_UID_ADDRESS + 0x04);
// uint32_t* uid32ptr_2 = (uint32_t*) (STM32_UID_ADDRESS + 0x08);

// uint32_t uid32val_0 = *uid32ptr_0;
// uint32_t uid32val_1 = *uid32ptr_1;
// uint32_t uid32val_2 = *uid32ptr_2;

// MCU_UID[0x00] = (uid32val_0 >> 0x00) & 0xFF;
// MCU_UID[0x01] = (uid32val_0 >> 0x08) & 0xFF;
// MCU_UID[0x02] = (uid32val_0 >> 0x10) & 0xFF;
// MCU_UID[0x03] = (uid32val_0 >> 0x18) & 0xFF;
// MCU_UID[0x04] = (uid32val_1 >> 0x00) & 0xFF;
// MCU_UID[0x05] = (uid32val_1 >> 0x08) & 0xFF;
// MCU_UID[0x06] = (uid32val_1 >> 0x10) & 0xFF;
// MCU_UID[0x07] = (uid32val_1 >> 0x18) & 0xFF;
// MCU_UID[0x08] = (uid32val_2 >> 0x00) & 0xFF;
// MCU_UID[0x09] = (uid32val_2 >> 0x08) & 0xFF;
// MCU_UID[0x0A] = (uid32val_2 >> 0x10) & 0xFF;
// MCU_UID[0x0B] = (uid32val_2 >> 0x18) & 0xFF;

uint8_t getIdPart(uint32_t id_ptr, uint32_t segment, uint8_t part) {
    uint32_t id = *((uint32_t*) (id_ptr + (segment * 4)));
    return (id >> (part * 8)) & 0xFF;
}

const uint8_t MCU_UID[12] = {
    getIdPart(STM32_UID_ADDRESS, 0, 0),
    getIdPart(STM32_UID_ADDRESS, 0, 1),
    getIdPart(STM32_UID_ADDRESS, 0, 2),
    getIdPart(STM32_UID_ADDRESS, 0, 3),
    getIdPart(STM32_UID_ADDRESS, 1, 0),
    getIdPart(STM32_UID_ADDRESS, 1, 1),
    getIdPart(STM32_UID_ADDRESS, 1, 2),
    getIdPart(STM32_UID_ADDRESS, 1, 3),
    getIdPart(STM32_UID_ADDRESS, 2, 0),
    getIdPart(STM32_UID_ADDRESS, 2, 1),
    getIdPart(STM32_UID_ADDRESS, 2, 2),
    getIdPart(STM32_UID_ADDRESS, 2, 3),
};


const char* project_path = __FILE__;
const char* project_date = __DATE__;
const char* project_time = __TIME__;
const char* build_number = TOSTRING(BUILD_NUMBER);

void printDeviceUID() {
    Serial.print("MCU UID: 0x");
    for (int i = 0; i < 12; i++) {
        Serial.printf("%02X", MCU_UID[i]);
    }
    Serial.println();
}