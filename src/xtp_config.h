#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IWatchdog.h>

#include "mcu_tools.h"
#include "iec_time.h"

// #define STM32_UID_ADDRESS  0x1FFFF7E8    // STM32F1
#define STM32_UID_ADDRESS  0x1FFF7A10    // STM32F4
// #define STM32_UID_ADDRESS  DBGMCU_BASE  // STM32 universal ???

#ifndef OTA_STORAGE_STM32_SECTOR
#define OTA_STORAGE_STM32_SECTOR 6
#endif // OTA_STORAGE_STM32_SECTOR

#ifdef LED_BUILTIN
#undef LED_BUILTIN
#endif // LED_BUILTIN
#define LED_BUILTIN PC13

#ifndef RETAINED_DATA_FLASH_ADDRESS
#define RETAINED_DATA_FLASH_ADDRESS 1000
#endif // RETAINED_DATA_FLASH_ADDRESS

#include "xtp_tools.h"
#include "xtp_retain.h"

#define THREAD_PERIOD_US 100

#ifdef XTP_12A6_E

// ######### GENERAL GPIO PINS #########
#define ENC_A_pin       PC12
#define ENC_B_pin       PC11

#define INPUT_0_pin     PC10
#define INPUT_1_pin     PA15
#define INPUT_2_pin     PA12
#define INPUT_3_pin     PA11
#define INPUT_4_pin     PA8
#define INPUT_5_pin     PC9
#define INPUT_6_pin     PC8
#define INPUT_7_pin     PC7

#define OUTPUT_0_pin    PC0
#define OUTPUT_1_pin    PC1
#define OUTPUT_2_pin    PC2
#define OUTPUT_3_pin    PC3

#define ANALOG_0_pin    PA0
#define ANALOG_1_pin    PA1
#define ANALOG_2_pin    PA2
#define ANALOG_3_pin    PA3
#define ANALOG_4_pin    PA4
#define ANALOG_5_pin    PA5

#define MISC_0_pin      PB1
#define MISC_1_pin      PB0
#define MISC_2_pin      PC5
#define MISC_3_pin      PC4
#define MISC_4_pin      PA7
#define MISC_5_pin      PA6

// ######### PERIPHERAL GPIO PINS #########
#define ANALOG_24V_pin  PD2

#define BUTTON_pin      PB2

#define ETH_RST_pin     PB10
#define ETH_CS_pin      PB9

#define FLASH_CS_pin    PC6

#define AUX_CS_pin      PB12
#define AUX_LATCH_pin   PB8
#define AUX_ENABLE_pin  PB5
#define AUX_RESET_pin   PB4

#define SPI_SCK_pin     PB13
#define SPI_MISO_pin    PB14
#define SPI_MOSI_pin    PB15

#define SPI_EXP_SCK_pin     PB13
#define SPI_EXP_MISO_pin    PB14
#define SPI_EXP_MOSI_pin    PB15

#define I2C_WP_pin      PB3

#define I2C_SDA_pin     PB7
#define I2C_SCL_pin     PB6

#define UART_RX_pin     PA10
#define UART_TX_pin     PA9

#elif defined(XTP_14A6_E)


// ######### GENERAL GPIO PINS #########
#define ENC_A_pin           PC12
#define ENC_B_pin           PC11

#define INPUT_0_pin         PC12
#define INPUT_1_pin         PC11
#define INPUT_2_pin         PC10
#define INPUT_3_pin         PA15
#define INPUT_4_pin         PA12
#define INPUT_5_pin         PA11
#define INPUT_6_pin         PA8
#define INPUT_7_pin         PC9
#define INPUT_8_pin         PC8
#define INPUT_9_pin         PC7

#define OUTPUT_0_pin        PC0
#define OUTPUT_1_pin        PC1
#define OUTPUT_2_pin        PC2
#define OUTPUT_3_pin        PC3

#define ANALOG_0_pin        PA0
#define ANALOG_1_pin        PA1
#define ANALOG_2_pin        PA2
#define ANALOG_3_pin        PA3
#define ANALOG_4_pin        PA4
#define ANALOG_5_pin        PC5

#define MISC_0_pin          PB1
#define MISC_1_pin          PB0

// ######### PERIPHERAL GPIO PINS #########
#define ANALOG_24V_pin      PC4

#define BUTTON_pin          PD2

#define ETH_RST_pin         PB9
#define ETH_CS_pin          PB10

#define FLASH_CS_pin        PC6

#define AUX_CS_pin          PB12
#define AUX_LATCH_pin       PB8
#define AUX_ENABLE_pin      PB5
#define AUX_RESET_pin       PB4


#define SPI_SCK_pin         PB13
#define SPI_MISO_pin        PB14
#define SPI_MOSI_pin        PB15

#define SPI_EXP_SCK_pin     PA5
#define SPI_EXP_MISO_pin    PA6
#define SPI_EXP_MOSI_pin    PA7

#define I2C_WP_pin          PB3 // not connected

#define I2C_SDA_pin         PB7
#define I2C_SCL_pin         PB6

#define UART_RX_pin         PA10
#define UART_TX_pin         PA9

#else // UNKNOWN BOARD

#error "Unknown board" \
       "Please define one of the following boards before importing this library:" \
       "#define XTP_12A6_E" \
       "#define XTP_14A6_E"

#endif // BOARD

#if (SPI_SCK_pin == SPI_EXP_SCK_pin) 
#define SPI_IS_SHARED 
#endif // SPI_SCK_pin == SPI_EXP_SCK_pin

// ######### PERIPHERAL SETTINGS #########
#define I2C_CLOCK       200000
#define UART_BAUDRATE   115200


#ifndef DEVICE_NAME
#if defined(XTP_12A6_E)
#define DEVICE_NAME "XTP12A6E"
#elif defined(XTP_14A6_E)
#define DEVICE_NAME "XTP14A6E"
#else
#define DEVICE_NAME "UNKNOWN"
#endif // XTP_12A6_E
#endif // DEVICE_NAME

char DEFAULT_DEVICE_NAME[32] = DEVICE_NAME;


DynamicJsonDocument json_buffer(8192);
char json_buffer_str[8192];

unsigned int local_port = 80;  // local port to listen on
byte local_mac[6];
byte local_ip[4];


char msg[256];
