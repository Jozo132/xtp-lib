#pragma once

#include <Arduino.h>
#include "stm32h7xx_hal.h"
#include "xtp_thread.h"

#if !defined(XTP_3)
#error "xtp_lsr8io.h requires XTP_3"
#endif

#ifndef XTP_LSR8IO_BUTTON_BIT
#define XTP_LSR8IO_BUTTON_BIT 4
#endif
#ifndef XTP_LSR8IO_LED_ERR_BIT
#define XTP_LSR8IO_LED_ERR_BIT 4
#endif
#ifndef XTP_LSR8IO_LED_RUN_BIT
#define XTP_LSR8IO_LED_RUN_BIT 6
#endif

namespace xtp_lsr8io {

constexpr uint8_t BUTTON_BIT = XTP_LSR8IO_BUTTON_BIT;
constexpr uint8_t LED_ERR_BIT = XTP_LSR8IO_LED_ERR_BIT;
constexpr uint8_t LED_RUN_BIT = XTP_LSR8IO_LED_RUN_BIT;
constexpr uint32_t LATCH_MASK = 1u << 7;
constexpr uint32_t ENABLE_MASK = 1u << 5;
constexpr uint32_t PERIOD_US = 20;
constexpr uint32_t LED_RUN_PERIOD_MS = 500;
constexpr uint32_t LED_RUN_ACTIVE_MS = 25;

SPI_HandleTypeDef spi;
volatile uint8_t inputState = 0;
volatile uint8_t outputState = 0;

extern "C" void HAL_SPI_MspInit(SPI_HandleTypeDef *handle) {
  if (handle->Instance != SPI3) {
    return;
  }

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_SPI3_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF6_SPI3;
  gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Alternate = GPIO_AF5_SPI3;
  gpio.Pin = GPIO_PIN_6;
  HAL_GPIO_Init(GPIOD, &gpio);
}

inline void writeGpio(GPIO_TypeDef *port, uint32_t mask, bool high) {
  port->BSRR = high ? mask : mask << 16;
}

inline bool initSpi() {
  spi.Instance = SPI3;
  spi.Init.Mode = SPI_MODE_MASTER;
  spi.Init.Direction = SPI_DIRECTION_2LINES;
  spi.Init.DataSize = SPI_DATASIZE_8BIT;
  spi.Init.CLKPolarity = SPI_POLARITY_LOW;
  spi.Init.CLKPhase = SPI_PHASE_1EDGE;
  spi.Init.NSS = SPI_NSS_SOFT;
  spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  spi.Init.TIMode = SPI_TIMODE_DISABLE;
  spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  spi.Init.CRCPolynomial = 7;
  spi.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLED;
  spi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  spi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  spi.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ONE_PATTERN;
  spi.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ONE_PATTERN;
  spi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  spi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  spi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  spi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  spi.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  return HAL_SPI_Init(&spi) == HAL_OK;
}

inline uint8_t transfer(uint8_t value) {
  writeGpio(GPIOD, LATCH_MASK, true);
  delayMicroseconds(1);

  uint8_t received = 0;
  if (HAL_SPI_TransmitReceive(&spi, &value, &received, 1, 1) != HAL_OK) {
    received = 0;
  }
  delayMicroseconds(1);

  writeGpio(GPIOD, LATCH_MASK, false);
  delayMicroseconds(1);
  writeGpio(GPIOD, LATCH_MASK, true);
  return received;
}

inline void task() {
  const uint32_t now = millis();
  const bool heartbeat = (now % LED_RUN_PERIOD_MS) < LED_RUN_ACTIVE_MS;

  uint8_t value = outputState & static_cast<uint8_t>(~_BV(LED_RUN_BIT));
  if (heartbeat) {
    value |= _BV(LED_RUN_BIT);
  }
  inputState = transfer(value);
}

inline bool begin() {
  pinMode(PD5, OUTPUT);
  pinMode(PD7, OUTPUT);
  writeGpio(GPIOD, ENABLE_MASK, false);
  writeGpio(GPIOD, LATCH_MASK, true);
  if (!initSpi()) {
    return false;
  }
  transfer(0);
  thread_setup(PERIOD_US, task);
  return true;
}

inline uint8_t readInput() {
  return inputState;
}

inline void writeOutput(uint8_t value) {
  outputState = value;
}

}