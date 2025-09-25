#pragma once

#include <Arduino.h>
#include <SPIMemory.h>

#include "xtp_config.h"
#include "xtp_spi.h"
#include "xtp_retain.h"

SPIFlash flash(FLASH_CS_pin);

void flash_print_info();

uint8_t _flash_retain_image[RETAINED_DATA_SIZE];

void _flash_write(bool force = false);
void _flash_read();

bool flash_initialized = false;
void flash_setup() {
    if (flash_initialized) return;
    spi_select(SPI_Flash);
    flash.begin();
    if (flash.error()) {
        Serial.println(flash.error(VERBOSE));
        return;
    } else {
        flash_initialized = true;
        Serial.printf("FLASH initialized.\n");
        flash_print_info();
    }
    uint32_t t = micros();
    _flash_read();
    uint32_t elapsed = micros() - t;
    Serial.printf("FLASH retained data [%d] read in %d us\n", RETAINED_DATA_SIZE, elapsed);
    if (retainedData.reboot_count == -1) {
        Serial.println("FLASH retained data not found. Writing default data");
        memcpy(&retainedData, &retainedDataDefault, RETAINED_DATA_SIZE);
        _flash_write();
    } else {
        retainedData.reboot_count++;
        _flash_write();
    }
    spi_select(SPI_None);
}


void flash_firmware_reset() {
    if (!flash_initialized) {
        Serial.println("FLASH not initialized. Cannot reset firmware");
        return;
    }
    spi_select(SPI_Flash);
    Serial.println("FLASH resetting retained data to firmware default");
    int reboot_count = retainedData.reboot_count;
    int write_count = retainedData.write_count;
    int firmware_reset_count = retainedData.firmware_reset_count + 1;
    memcpy(&retainedData, &retainedDataDefault, RETAINED_DATA_SIZE);
    retainedData.reboot_count = reboot_count;
    retainedData.write_count = write_count;
    retainedData.firmware_reset_count = firmware_reset_count;
    _flash_write();
    spi_select(SPI_None);
}


void flash_store_retained_data() {
    if (!flash_initialized) {
        Serial.println("FLASH not initialized. Cannot store retained data");
        return;
    }
    spi_select(SPI_Flash);
    _flash_write();
    spi_select(SPI_None);
}


bool _flash_info_checked = false;
void flash_print_info() {

    if (!_flash_info_checked) {
        if (!flash_initialized) {
            Serial.println("FLASH not initialized. Cannot get ID");
            return;
        }
        spi_select(SPI_Flash);
        _flash_info_checked = true;
        flashInfo.JEDEC = flash.getJEDECID();
        flashInfo.manufacturer_id = flashInfo.JEDEC >> 16 & 0xFF;
        flashInfo.memory_id = flashInfo.JEDEC >> 8 & 0xFF;
        uint64_t uid64 = flash.getUniqueID();
        flashInfo.uid_a = uid64 >> 32;
        flashInfo.uid_b = uid64 & 0xFFFFFFFF;
        flashInfo.size = flash.getCapacity();
        flashInfo.max_page = flash.getMaxPage();
        spi_select(SPI_None);
    }

    // Print in JSON format
    Serial.println("flash_info: {");
    Serial.printf("  manufacturer_id: 0x%02X,\n", flashInfo.manufacturer_id);
    Serial.printf("  memory_id: 0x%02X,\n", flashInfo.memory_id);
    Serial.printf("  unique_id: 0x%08X%08X,\n", flashInfo.uid_a, flashInfo.uid_b);
    Serial.printf("  size: %d,\n", flashInfo.size);
    Serial.printf("  max_page: %d\n", flashInfo.max_page);
    Serial.println("}");
}


void _flash_write(bool force) {
    uint8_t* actual = (uint8_t*) &retainedData;
    uint8_t* image = _flash_retain_image;
    bool different = false;
    for (int i = 0; i < RETAINED_DATA_SIZE; i++) {
        if (actual[i] != image[i]) {
            different = true;
            image[i] = actual[i];
        }
    }
    if (different) {
        if (force) {
            flash.eraseSection(RETAINED_DATA_FLASH_ADDRESS, RETAINED_DATA_SIZE);
            flash.writeByteArray(RETAINED_DATA_FLASH_ADDRESS, _flash_retain_image, RETAINED_DATA_SIZE);
            Serial.println("FLASH retained data stored");
            // delay(10);
            // printRetainedData();
        } else {
            retainedData.write_count++;
            _flash_write(true);
        }
    }
}

void _flash_read() {
    flash.readByteArray(RETAINED_DATA_FLASH_ADDRESS, _flash_retain_image, RETAINED_DATA_SIZE);
    memcpy(&retainedData, _flash_retain_image, RETAINED_DATA_SIZE);
}
