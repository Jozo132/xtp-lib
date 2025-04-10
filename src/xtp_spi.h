#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "xtp_config.h"

// Create the SPI port using SPI2 on pins PB15, PB14, and PB13
// SPIClass MySPI(SPI_MOSI_pin, SPI_MISO_pin, SPI_SCK_pin);

void spi_setup() {
    SPI.setMOSI(SPI_MOSI_pin);
    SPI.setMISO(SPI_MISO_pin);
    SPI.setSCLK(SPI_SCK_pin);
}

enum SPIDeviceSelect_t {
    SPI_None = 0,
    SPI_Ethernet,
    SPI_Flash,
    SPI_Expansion
};


void spi_select(SPIDeviceSelect_t device = SPI_None) {
    #ifdef SPI_IS_SHARED
    if (device != SPI_Ethernet) digitalWrite(ETH_CS_pin, HIGH);
    if (device != SPI_Flash) digitalWrite(FLASH_CS_pin, HIGH);
    if (device != SPI_Expansion) digitalWrite(AUX_CS_pin, HIGH);
    switch (device) {
        case SPI_Ethernet: digitalWrite(ETH_CS_pin, LOW); break;
        case SPI_Flash: digitalWrite(FLASH_CS_pin, LOW); break;
        case SPI_Expansion: digitalWrite(AUX_CS_pin, LOW); break;
        default: break;
    }
    #else // Expansion is on SPI1 while Ethernet and Flash are on SPI2
    if (device != SPI_Ethernet) digitalWrite(ETH_CS_pin, HIGH);
    if (device != SPI_Flash) digitalWrite(FLASH_CS_pin, HIGH);
    switch (device) {
        case SPI_Ethernet: digitalWrite(ETH_CS_pin, LOW); break;
        case SPI_Flash: digitalWrite(FLASH_CS_pin, LOW); break;
        default: break;
    }
    #endif
}

void spi_select_exp(SPIDeviceSelect_t device = SPI_None) {
    #ifdef SPI_IS_SHARED
    return spi_select(device);
    #else
    if (device != SPI_Expansion) digitalWrite(AUX_CS_pin, HIGH);
    else digitalWrite(AUX_CS_pin, LOW);
    #endif
}