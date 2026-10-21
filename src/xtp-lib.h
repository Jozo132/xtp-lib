#pragma once

#include <Arduino.h>
#include <SPI.h>

// Include timing telemetry header early (before other headers use it)
#include "xtp_timing.h"

#ifdef XTP_12A6_E
#define XTP_DEVICE_NAME "XTP12A6E"
#endif // XTP_DEVICE_NAME
#ifdef XTP_14A6_E
#define XTP_DEVICE_NAME "XTP14A6E"
#endif // XTP_DEVICE_NAME
#ifdef XTP_3
#define XTP_DEVICE_NAME "XTP3"
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
#if !defined(XTP_3) || defined(XTP3_ENABLE_LOCAL_HARDWARE) || defined(NOTA_EXT_FLASH_OTA)
#if !defined(XTP_3) || defined(XTP3_ENABLE_LOCAL_HARDWARE)
#include "xtp_gpio.h"
#include "xtp_uart.h"
#endif
#include "xtp_flash.h"
#endif
#include "xtp_oled.h"
#if defined(XTP_3)
#include "xtp_aht20.h"
#endif
#if defined(XTP_3)
#include "xtp_ethernet_lwip.h"
#else
#include "xtp_ethernet.h"
#endif
#include "xtp_thread.h"
#include "xtp_time.h"
#if !defined(XTP_3)
#include "xtp_sntp.h"
#include "xtp_ota.h"
#else
#include "xtp_sntp.h"
#include <NOTA.h>
#endif

#include "iec_time.h"
#if defined(XTP_3)
using XtpOtaCallback = void (*)();
inline XtpOtaCallback ota_shutdown = nullptr;
inline bool xtp3_ota_started = false;
inline NOTAClass xtp3_ota;
inline void ota_setup() {
  xtp3_ota.setHostname(XTP3_ETH_HOSTNAME);
  xtp3_ota.setPlatform("STM32");
  xtp3_ota.setBoard("STM32H723VGT6");
  xtp3_ota.setVersion(XTP_DEVICE_NAME);
  xtp3_ota.setPort(3232);
  xtp3_ota.setRebootOnSuccess(true);
}
inline void ota_loop() {
  if (!ethernet_ready()) return;
  if (!xtp3_ota_started) {
    xtp3_ota.begin();
    xtp3_ota_started = true;
  }
  xtp3_ota.handle();
}
#else
#include "ota.h"
#endif

#include "xtp_status.h"

volatile uint8_t xtp_loop_stage = 0;

#if !defined(XTP_3)
#include "rest_server.h"
#include "xtp_http_server.h"
#include "xtp_tcp_client.h"
#endif
#ifdef XTP_WEBSOCKETS
#if !defined(XTP_3)
#include "xtp_websocket.h"
#include "xtp_ws_client.h"
#endif
#endif

void xtp_setup() {
  IWatchdog.begin(60000000L);
  IWatchdog.reload();
  XTP_TIMING_INIT();  // Initialize timing telemetry
#if !defined(XTP_3) || defined(XTP3_ENABLE_LOCAL_HARDWARE)
  gpio_setup();
  for (int i = 0; i < 10; i++) {
    digitalToggle(LED_BUILTIN);
    delay(20);
  }
  digitalWrite(LED_BUILTIN, HIGH);
  uart_setup();
#endif
#if !defined(XTP_3) || defined(XTP3_ENABLE_LOCAL_HARDWARE) || defined(NOTA_EXT_FLASH_OTA)
  spi_setup();
  flash_setup();
  IWatchdog.reload();
#endif
  getDeviceUID();
  time_setup();
  i2c_setup();
#if defined(XTP_3)
  aht20_setup();
#endif
  oled_setup();
  ethernet_setup();
#if !defined(XTP_3)
  ota_shutdown = []() { thread_pause(); };
#endif
  ota_setup();
  xtp_status_setup();
  
  web_server_setup();
#ifdef XTP_WEBSOCKETS
  xtp_ws_setup(); // Websocket setup
#endif
  IWatchdog.reload();
}

void xtp_loop() {
  XTP_TIMING_START(XTP_TIME_LOOP_TOTAL);
  
  IWatchdog.reload();

#if defined(XTP_3)
  aht20_loop();
#endif
  xtp_loop_stage = 1;
  
#if !defined(XTP_3) || defined(XTP3_ENABLE_LOCAL_HARDWARE)
  XTP_TIMING_START(XTP_TIME_I2C_LOOP);
  i2c_loop();
  XTP_TIMING_END(XTP_TIME_I2C_LOOP);
#endif
  xtp_loop_stage = 2;

  XTP_TIMING_START(XTP_TIME_OLED_UPDATE);
  oled_state_machine_update();
  XTP_TIMING_END(XTP_TIME_OLED_UPDATE);
  xtp_loop_stage = 3;
  
  XTP_TIMING_START(XTP_TIME_ETH_LOOP);
  ethernet_loop();
#ifdef XTP_WEBSOCKETS
  xtp_ws_loop(); // Websocket loop
#endif
  XTP_TIMING_END(XTP_TIME_ETH_LOOP);
  xtp_loop_stage = 4;
  
  XTP_TIMING_START(XTP_TIME_OTA_LOOP);
  ota_loop();
  XTP_TIMING_END(XTP_TIME_OTA_LOOP);
  xtp_loop_stage = 5;

  xtp_loop_stage = 10;
  xtp_status_loop();
  xtp_loop_stage = 6;
  
  XTP_TIMING_START(XTP_TIME_OLED_TICKER);
  oled_ticker();
  XTP_TIMING_END(XTP_TIME_OLED_TICKER);
  xtp_loop_stage = 7;
  
  XTP_TIMING_END(XTP_TIME_LOOP_TOTAL);
}