#pragma once

#include <Arduino.h>

static HardwareTimer _thread_Timer(TIM2);

typedef void (*thread_handle_t)(void); // Define void thread_handle function type
thread_handle_t _thread_handler = nullptr; // Define thread_handle function pointer

long thread_HAL_time = 0;
long thread_HAL_time_max = 0;
long thread_HAL_time_min = 0;

bool thread_busy = false;
bool thread_enabled = false;
void thread_loop() {
    if (thread_busy || !thread_enabled) return; // If thread is busy, return (do not execute the given thread function
    thread_busy = true;
    long thread_NEW_time = micros();
    if (_thread_handler) _thread_handler(); // Execute the given thread function
    thread_HAL_time = micros() - thread_NEW_time;
    if (thread_HAL_time > thread_HAL_time_max) thread_HAL_time_max = thread_HAL_time;
    if (thread_HAL_time < thread_HAL_time_min || thread_HAL_time_min == 0) thread_HAL_time_min = thread_HAL_time;
    thread_busy = false;
}

void thread_onEvent(thread_handle_t handler) {
    _thread_handler = handler;
}

void thread_setup(uint32_t period_us, thread_handle_t handler = nullptr) {
    if (handler) thread_onEvent(handler);
    if (period_us < 20) period_us = 20; // Minimum period is 20us (50kHz)
    thread_enabled = true;
    _thread_Timer.pause();
    _thread_Timer.setOverflow(period_us, MICROSEC_FORMAT);
    _thread_Timer.attachInterrupt(thread_loop);
    _thread_Timer.refresh();
    _thread_Timer.resume();
    _thread_Timer.setInterruptPriority(15, 0);      // lowest for TIM2
}

void thread_pause() {
    thread_enabled = false;
    _thread_Timer.pause();
}
void thread_resume() {
    thread_enabled = true;
    _thread_Timer.resume();
}