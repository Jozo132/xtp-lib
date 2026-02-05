#pragma once

#include <Arduino.h>

// Include timing telemetry header early (before other headers use it)
#include "xtp_timing.h"

#ifdef XTP_12A6_E
#define XTP_DEVICE_NAME "XTP12A6E"
#endif // XTP_DEVICE_NAME
#ifdef XTP_14A6_E
#define XTP_DEVICE_NAME "XTP14A6E"
#endif // XTP_DEVICE_NAME

#ifndef XTP_DEVICE_NAME
#define XTP_DEVICE_NAME "XTP?"
#endif // XTP_DEVICE_NAME

#ifndef ARCH_STM32
#define ARCH_STM32
#endif // ARCH_STM32
#ifndef STM32
#define STM32
#endif // STM32
#include "xtp_config.h"
#include "xtp_tools.h"
#include "xtp_gpio.h"
#include "xtp_uart.h"
#include "xtp_oled.h"
#include "xtp_flash.h"
#include "xtp_ethernet.h"
#include "xtp_thread.h"
#include "xtp_time.h"
#include "xtp_sntp.h"
#include "xtp_ota.h"

#include "iec_time.h"
#include "ota.h"

#include "rest_server.h"
#include "xtp_http_server.h"
#ifdef XTP_WEBSOCKETS
#include "xtp_websocket.h"
#endif

void xtp_setup() {
  IWatchdog.begin(60000000L);
  IWatchdog.reload();
  XTP_TIMING_INIT();  // Initialize timing telemetry
  gpio_setup();
  for (int i = 0; i < 10; i++) {
    digitalToggle(LED_BUILTIN);
    delay(20);
  }
  digitalWrite(LED_BUILTIN, HIGH);
  uart_setup();
  spi_setup();
  flash_setup();
  IWatchdog.reload();
  getDeviceUID();
  time_setup();
  // printDeviceUID();
  i2c_setup();
  oled_setup();
  ethernet_setup();
  ota_shutdown = []() { thread_pause(); };
  ota_setup();
  
  web_server_setup();
#ifdef XTP_WEBSOCKETS
  xtp_ws_setup(); // Websocket setup
#endif
  IWatchdog.reload();
}

void xtp_loop() {
  XTP_TIMING_START(XTP_TIME_LOOP_TOTAL);
  
  IWatchdog.reload();
  
  XTP_TIMING_START(XTP_TIME_I2C_LOOP);
  i2c_loop();                   // I2C bus maintenance & auto-recovery
  XTP_TIMING_END(XTP_TIME_I2C_LOOP);
  
  XTP_TIMING_START(XTP_TIME_OLED_UPDATE);
  oled_state_machine_update();  // Non-blocking OLED updates
  XTP_TIMING_END(XTP_TIME_OLED_UPDATE);
  
  XTP_TIMING_START(XTP_TIME_ETH_LOOP);
  ethernet_loop();
#ifdef XTP_WEBSOCKETS
  xtp_ws_loop(); // Websocket loop
#endif
  XTP_TIMING_END(XTP_TIME_ETH_LOOP);
  
  XTP_TIMING_START(XTP_TIME_OTA_LOOP);
  ota_loop();
  XTP_TIMING_END(XTP_TIME_OTA_LOOP);
  
  XTP_TIMING_START(XTP_TIME_OLED_TICKER);
  oled_ticker();
  XTP_TIMING_END(XTP_TIME_OLED_TICKER);
  
  XTP_TIMING_END(XTP_TIME_LOOP_TOTAL);
}