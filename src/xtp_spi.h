#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "xtp_config.h"

// Create the SPI port using SPI2 on pins PB15, PB14, and PB13
// SPIClass MySPI(SPI_MOSI_pin, SPI_MISO_pin, SPI_SCK_pin);

static SPISettings ETH_SPI_SET(ETH_SPI_SPEED, MSBFIRST, SPI_MODE0);
static SPISettings FLASH_SPI_SET(FLASH_SPI_SPEED, MSBFIRST, SPI_MODE0);
static SPISettings AUX_SPI_SET(AUX_SPI_SPEED, MSBFIRST, SPI_MODE0);

#if defined(AUX_MOSI_pin) && defined(AUX_MISO_pin) && defined(AUX_SCK_pin)
#define EXP_SPI
SPIClass SPI_EXP;
#endif // AUX SPI pins defined -> enable EXP_SPI
bool spi_initialized = false;
void spi_setup() {
    if (spi_initialized) return;
    spi_initialized = true;
    SPI.setMOSI(SPI_MOSI_pin);
    SPI.setMISO(SPI_MISO_pin);
    SPI.setSCLK(SPI_SCK_pin);

#ifdef EXP_SPI
    SPI_EXP.setMOSI(AUX_MOSI_pin);
    SPI_EXP.setMISO(AUX_MISO_pin);
    SPI_EXP.setSCLK(AUX_SCK_pin);
    SPI_EXP.setSSEL(AUX_CS_pin);
    SPI_EXP.begin();
#endif // EXP_SPI
}

enum SPIDeviceSelect_t {
    SPI_None = 0,
    SPI_Ethernet,
    SPI_Flash,
    SPI_Expansion
};

SPIDeviceSelect_t current_spi_device = SPI_None;

void spi_select(SPIDeviceSelect_t device = SPI_None) {
    if (current_spi_device == device) return;
    if (current_spi_device != SPI_None) SPI.endTransaction();
#ifdef SPI_IS_SHARED
    if (device != SPI_Ethernet) digitalWrite(ETH_CS_pin, HIGH);
    if (device != SPI_Flash) digitalWrite(FLASH_CS_pin, HIGH);
    if (device != SPI_Expansion) digitalWrite(AUX_CS_pin, HIGH);
    switch (device) {
        case SPI_Ethernet: {
            digitalWrite(ETH_CS_pin, LOW);
            SPI.beginTransaction(ETH_SPI_SET);
            break;
        }
        case SPI_Flash: {
            digitalWrite(FLASH_CS_pin, LOW);
            SPI.beginTransaction(FLASH_SPI_SET);
            break;
        }
        case SPI_Expansion: {
            digitalWrite(AUX_CS_pin, LOW);
            SPI.beginTransaction(AUX_SPI_SET);
            break;
        }
        default: {
            SPI.endTransaction();
            break;
        }
    }
#else // Expansion is on SPI1 while Ethernet and Flash are on SPI2
    if (device != SPI_Ethernet) digitalWrite(ETH_CS_pin, HIGH);
    if (device != SPI_Flash) digitalWrite(FLASH_CS_pin, HIGH);
    switch (device) {
        case SPI_Ethernet: {
            digitalWrite(ETH_CS_pin, LOW);
            SPI.beginTransaction(ETH_SPI_SET);
            break;
        }
        case SPI_Flash: {
            digitalWrite(FLASH_CS_pin, LOW);
            SPI.beginTransaction(FLASH_SPI_SET);
            break;
        }
        default: {
            SPI.endTransaction();
            break;
        }
    }
#endif // SPI_IS_SHARED
}
#ifndef SPI_IS_SHARED
SPIDeviceSelect_t current_spi_exp_device = SPI_None;
#endif // SPI_IS_SHARED

void spi_select_exp(SPIDeviceSelect_t device = SPI_None) {
#ifdef SPI_IS_SHARED
    return spi_select(device);
#else // Expansion is on SPI1 while Ethernet and Flash are on SPI2
    if (current_spi_exp_device == device) return;
    if (current_spi_exp_device != SPI_None) SPI_EXP.endTransaction();
    if (device != SPI_Expansion) digitalWrite(AUX_CS_pin, HIGH);
    switch (device) {
        case SPI_Expansion: {
            digitalWrite(AUX_CS_pin, LOW);
            SPI_EXP.beginTransaction(AUX_SPI_SET);
            break;
        }
        default: {
            SPI_EXP.endTransaction();
            break;
        }
    }
#endif // SPI_IS_SHARED
}