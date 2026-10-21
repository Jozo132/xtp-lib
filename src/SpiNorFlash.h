#pragma once

#include <Arduino.h>
#include <SPI.h>

class SpiNorFlash {
public:
    enum ErrorCode : uint8_t {
        Success = 0,
        CallBegin = 1,
        NoResponse = 2,
        UnknownCapacity = 3,
        Busy = 4,
        OutOfBounds = 5,
        WriteEnableFailed = 6,
        ProgramFailed = 10,
        Unsupported = 12
    };

    SpiNorFlash(uint8_t csPin, SPIClass& spi);

    void setClock(uint32_t clockHz);
    bool begin(uint32_t capacityBytes = 0);
    uint8_t error(bool verbose = false) const;

    uint32_t getJEDECID() const;
    uint64_t getUniqueID();
    uint32_t getCapacity() const;
    uint32_t getMaxPage() const;

    bool readByteArray(uint32_t address, uint8_t* data, size_t length);
    uint8_t readByte(uint32_t address);
    bool writeByteArray(uint32_t address, const uint8_t* data, size_t length);
    bool writeByte(uint32_t address, uint8_t data);
    bool eraseSector(uint32_t address);
    bool eraseSection(uint32_t address, uint32_t length);

private:
    static constexpr uint8_t ReadData = 0x03;
    static constexpr uint8_t PageProgram = 0x02;
    static constexpr uint8_t WriteEnable = 0x06;
    static constexpr uint8_t ReadStatus1 = 0x05;
    static constexpr uint8_t ReadJedecId = 0x9F;
    static constexpr uint8_t ReadUniqueId = 0x4B;
    static constexpr uint8_t SectorErase = 0x20;
    static constexpr uint8_t Enter4ByteAddress = 0xB7;
    static constexpr uint8_t Exit4ByteAddress = 0xE9;
    static constexpr uint8_t StatusBusy = 0x01;
    static constexpr uint8_t StatusWriteEnable = 0x02;

    static constexpr uint32_t DefaultSectorSize = 4096;
    static constexpr uint16_t DefaultPageSize = 256;
    static constexpr uint32_t FourByteAddressThreshold = 0x01000000UL;
    static constexpr uint32_t DefaultTimeoutMs = 5000;

    uint8_t _csPin;
    SPIClass& _spi;
    SPISettings _settings;
    uint32_t _capacity = 0;
    uint32_t _jedecId = 0;
    uint32_t _sectorSize = DefaultSectorSize;
    uint16_t _pageSize = DefaultPageSize;
    uint8_t _error = CallBegin;
    bool _begun = false;
    bool _address4Byte = false;

    void select();
    void deselect();
    uint8_t transfer(uint8_t value);
    void transferAddress(uint32_t address);
    void enter4ByteAddressing();
    void exit4ByteAddressing();
    bool prepareAddress(uint32_t address);
    void finishAddress(bool enteredForOperation);
    uint8_t readStatus1();
    bool waitReady(uint32_t timeoutMs = DefaultTimeoutMs);
    bool enableWrite();
    uint32_t capacityFromJedec(uint8_t capacityId) const;
    bool validRange(uint32_t address, uint32_t length) const;
    bool writePage(uint32_t address, const uint8_t* data, size_t length);
};