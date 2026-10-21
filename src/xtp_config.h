#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IWatchdog.h>

#include "mcu_tools.h"
#include "iec_time.h"

// #define STM32_UID_ADDRESS  0x1FFFF7E8    // STM32F1
#if defined(XTP_3)
#define STM32_UID_ADDRESS  0x1FF1E800UL   // STM32H7
#else
#define STM32_UID_ADDRESS  0x1FFF7A10UL   // STM32F4
#endif

#ifndef OTA_STORAGE_STM32_SECTOR
#define OTA_STORAGE_STM32_SECTOR 6
#endif // OTA_STORAGE_STM32_SECTOR

#ifdef LED_BUILTIN
#undef LED_BUILTIN
#endif // LED_BUILTIN
#if !defined(XTP_3)
#define LED_BUILTIN PC13
#endif

#ifndef RETAINED_DATA_FLASH_ADDRESS
#define RETAINED_DATA_FLASH_ADDRESS 1000
#endif // RETAINED_DATA_FLASH_ADDRESS

#include "xtp_tools.h"
#include "xtp_retain.h"

#ifndef THREAD_PERIOD_US
#define THREAD_PERIOD_US 1000 // 1ms
#endif // THREAD_PERIOD_US

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

#define AUX_SCK_pin     PB13
#define AUX_MISO_pin    PB14
#define AUX_MOSI_pin    PB15

#define I2C_WP_pin      PB3

#define I2C_SDA_pin     PB7
#define I2C_SCL_pin     PB6

#define UART_RX_pin     PA10
#define UART_TX_pin     PA9
#define XTP_USB_ENABLED 0

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

#define AUX_SCK_pin     PA5
#define AUX_MISO_pin    PA6
#define AUX_MOSI_pin    PA7

#define I2C_WP_pin          PB3 // not connected

#define I2C_SDA_pin         PB7
#define I2C_SCL_pin         PB6

#define UART_RX_pin         PA10
#define UART_TX_pin         PA9
#define XTP_USB_ENABLED     0

#elif defined(XTP_3)

// ######### XTP-3 STM32H723 LSR8IO BOARD #########
#ifdef XTP3_PINMAP_HEADER
#include XTP3_PINMAP_HEADER
#else
#define XTP_LSR8IO_SCK_pin       PB3
#define XTP_LSR8IO_MISO_pin      PB4
#define XTP_LSR8IO_MOSI_pin      PD6
#define XTP_LSR8IO_LATCH_pin     PD7
#define XTP_LSR8IO_ENABLE_pin    PD5
#define XTP_LSR8IO_BUTTON_BIT    4
#define XTP_LSR8IO_LED_ERR_BIT   4
#define XTP_LSR8IO_LED_RUN_BIT   6

#define USB_DM_pin                PA11
#define USB_DP_pin                PA12
#define SWDIO_pin                 PA13
#define SWCLK_pin                 PA14

#define ANALOG_0_pin              PA3
#define ANALOG_1_pin              PA4
#define ANALOG_2_pin              PA5
#define ANALOG_3_pin              PA6
#define ANALOG_4_pin              PB0
#define ANALOG_5_pin              PB1
#define ANALOG_24V_pin            PC0

#define FS_CS_pin                 PA8
#define FS_SCK_pin                PA9
#define FS_MISO_pin               PC2_C
#define FS_MOSI_pin               PC3_C

#define AUX_MISO_pin              PE5
#define AUX_MOSI_pin              PE6
#define AUX_CS_pin                PE3
#define AUX_SCK_pin               PE2
#define AUX_LATCH_pin             PE4
#define AUX_ENABLE_pin            PB9
#define AUX_RESET_pin             PE13

#define I2C_SCL_pin               PB8
#define I2C_SDA_pin               PB7

#define UART_RX_pin               PD13
#define UART_TX_pin               PD14
#define UART2_RX_pin              PE0
#define UART2_TX_pin              PE1
#define ESP12_RX_pin              PB5
#define ESP12_TX_pin              PB6
#define ONEWIRE_DATA_pin          PB15

#define CAN1_RX_pin               PD0
#define CAN1_TX_pin               PD1
#define CAN2_RX_pin               PD12
#define CAN2_TX_pin               PD13

#define LIN_RX_pin                PC7
#define LIN_TX_pin                PC6
#define LIN_SLP_pin               PD10

#define RS485_RX_pin              PE7
#define RS485_TX_pin              PE8
#define RS485_DE_pin              PE9

#define RS422_RX_pin              PD9
#define RS422_TX_pin              PB10
#define RS422_CTS_pin             PD11
#define RS422_RTS_pin             PB14

#define SDMMC1_CD_pin             PA15
#define SDMMC1_CLK_pin            PD2
#define SDMMC1_CMD_pin            PC12
#define SDMMC1_D0_pin             PC8
#define SDMMC1_D1_pin             PC9
#define SDMMC1_D2_pin             PC10
#define SDMMC1_D3_pin             PC11

#define ETH_RMII_REF_CLK_pin      PA1
#define ETH_RMII_RXD0_pin         PC4
#define ETH_RMII_RXD1_pin         PC5
#define ETH_RMII_TXD0_pin         PB12
#define ETH_RMII_TXD1_pin         PB13
#define ETH_RMII_TX_EN_pin        PB11
#define ETH_RMII_CRS_DV_pin       PA7
#define ETH_RMII_MDC_pin          PC1
#define ETH_RMII_MDIO_pin         PA2
#define ETH_PHY_INT_pin           PE15
#define ETH_PHY_RST_pin           PE14

#endif
#ifndef AHT20_I2C_ADDRESS
#define AHT20_I2C_ADDRESS         0x38
#endif
#define XTP_USB_ENABLED          1

#define XTP_DIRECT_INPUT_0_pin   PD4
#define XTP_DIRECT_INPUT_1_pin   PD3

#else // UNKNOWN BOARD

#error "Unknown board" \
       "Please define one of the following boards before importing this library:" \
       "#define XTP_12A6_E" \
       "#define XTP_14A6_E" \
       "#define XTP_3"

#endif // BOARD

#if !defined(XTP_3) && defined(SPI_SCK_pin) && defined(AUX_SCK_pin) && (SPI_SCK_pin == AUX_SCK_pin)
#define SPI_IS_SHARED 
#endif // SPI_SCK_pin == AUX_SCK_pin

// ######### PERIPHERAL SETTINGS #########

#ifndef I2C_CLOCK
#define I2C_CLOCK       200000
#endif // I2C_CLOCK

#ifndef UART_BAUDRATE
#define UART_BAUDRATE   115200
#endif // UART_BAUDRATE

#ifndef ETH_SPI_SPEED
#if defined(XTP_12A6_E)
#define ETH_SPI_SPEED   16000000
#else // XTP_14A6_E
#define ETH_SPI_SPEED   32000000
#endif // XTP_12A6_E
#endif // ETH_SPI_SPEED

#ifndef FLASH_SPI_SPEED
#define FLASH_SPI_SPEED 1000000
#endif // FLASH_SPI_SPEED

#ifndef AUX_SPI_SPEED
#define AUX_SPI_SPEED   10000000
#endif // AUX_SPI_SPEED


#ifndef DEVICE_NAME
#if defined(XTP_12A6_E)
#define DEVICE_NAME "XTP12A6E"
#elif defined(XTP_14A6_E)
#define DEVICE_NAME "XTP14A6E"
#elif defined(XTP_3)
#define DEVICE_NAME "XTP3"
#else
#define DEVICE_NAME "UNKNOWN"
#endif // XTP_12A6_E
#endif // DEVICE_NAME

char DEFAULT_DEVICE_NAME[32] = DEVICE_NAME;

#if !defined(XTP_3)
class RestServer;
#endif
class MyFileSystem;


DynamicJsonDocument json_buffer(8192);
char json_buffer_str[8192];

unsigned int local_port = 80;  // local port to listen on
byte local_mac[6];
byte local_ip[4];


char msg[256];


void web_server_loop();