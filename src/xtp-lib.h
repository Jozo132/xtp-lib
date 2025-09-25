#pragma once

#include <Arduino.h>

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

void xtp_setup() {
  IWatchdog.begin(60000000L);
  IWatchdog.reload();
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
  IWatchdog.reload();
}

void xtp_loop() {
  // IntervalGlobalLoopCheck();
  IWatchdog.reload();
  ethernet_loop();
  ota_loop();
  oled_ticker();
}