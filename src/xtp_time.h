#pragma once

#include <Arduino.h>

#include <STM32RTC.h>

#ifndef _TIMEZONE_OFFSET_DEFAULT_
#define _TIMEZONE_OFFSET_DEFAULT_ 1 * 60 * 60 // UTC+1
#endif // _TIMEZONE_OFFSET_DEFAULT_

uint32_t timezone_offset_seconds = _TIMEZONE_OFFSET_DEFAULT_;

STM32RTC& rtc = STM32RTC::getInstance();

void time_setup(uint32_t timezone_offset_sec = _TIMEZONE_OFFSET_DEFAULT_) {
    timezone_offset_seconds = timezone_offset_sec;
    rtc.begin(STM32RTC::HOUR_24);
    // HAL_RTCEx_SetSmoothCalib(&hrtc, RTC_SMOOTHCALIB_PERIOD_32SEC, RTC_SMOOTHCALIB_PLUSPULSES_RESET, 0x8);
}

void time_set(uint32_t timestamp_ms) {
    uint32_t s = timestamp_ms / 1000;
    uint32_t ms = timestamp_ms % 1000;
    rtc.setEpoch(s + timezone_offset_seconds, ms);
}

void time_set_seconds(uint32_t timestamp_s) {
    rtc.setEpoch(timestamp_s + timezone_offset_seconds);
}

struct tm time_get() {
    time_t rawtime = rtc.getEpoch();
    struct tm ts = *localtime(&rawtime);
    return ts;
}

uint32_t time_ms() {
    uint32_t sec = rtc.getSubSeconds() - timezone_offset_seconds;
    uint32_t epoch = rtc.getEpoch();
    return sec * 1000 + epoch;
}

void time_print(char* buffer) { // Format: "YYYY-MM-DD HH:MM:SS"
    struct tm ts = time_get();
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec);
}