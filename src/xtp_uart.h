#pragma once

#include <Arduino.h>


// Create the serial port
HardwareSerial Serial1(USART1);


bool uart_initialized = false;
void uart_setup() {
    if (uart_initialized) return;
    uart_initialized = true;
#ifndef HAL_PCD_MODULE_ENABLED 
    Serial.setRx(UART_RX_pin);
    Serial.setTx(UART_TX_pin);
    Serial.begin(UART_BAUDRATE);
#endif
    Serial.println("\n\n\n\nUART initialized");
}

