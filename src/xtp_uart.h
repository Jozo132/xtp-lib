#pragma once

#include <Arduino.h>


// Create the serial port
HardwareSerial Serial1(USART1);


void uart_setup() {
#ifndef HAL_PCD_MODULE_ENABLED 
    Serial.setRx(UART_RX_pin);
    Serial.setTx(UART_TX_pin);
    Serial.begin(UART_BAUDRATE);
#endif
    Serial.println("\n\n\n\nUART initialized");
}

