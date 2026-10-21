#include "SpiNorFlash.h"

SpiNorFlash::SpiNorFlash(uint8_t csPin, SPIClass& spi)
    : _csPin(csPin), _spi(spi), _settings(1000000, MSBFIRST, SPI_MODE0) {
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
}

void SpiNorFlash::setClock(uint32_t clockHz) {
    _settings = SPISettings(clockHz, MSBFIRST, SPI_MODE0);
}

void SpiNorFlash::select() {
    _spi.beginTransaction(_settings);
    digitalWrite(_csPin, LOW);
}

void SpiNorFlash::deselect() {
    digitalWrite(_csPin, HIGH);
    _spi.endTransaction();
}

uint8_t SpiNorFlash::transfer(uint8_t value) {
    return _spi.transfer(value);
}

void SpiNorFlash::transferAddress(uint32_t address) {
    if (_address4Byte) transfer(static_cast<uint8_t>(address >> 24));
    transfer(static_cast<uint8_t>(address >> 16));
    transfer(static_cast<uint8_t>(address >> 8));
    transfer(static_cast<uint8_t>(address));
}

void SpiNorFlash::enter4ByteAddressing() {
    if (_address4Byte) return;
    select();
    transfer(Enter4ByteAddress);
    deselect();
    _address4Byte = true;
}

void SpiNorFlash::exit4ByteAddressing() {
    if (!_address4Byte) return;
    select();
    transfer(Exit4ByteAddress);
    deselect();
    _address4Byte = false;
}

bool SpiNorFlash::prepareAddress(uint32_t address) {
    const bool needs4Byte = address >= FourByteAddressThreshold;
    if (needs4Byte && !_address4Byte) enter4ByteAddressing();
    return needs4Byte;
}

void SpiNorFlash::finishAddress(bool enteredForOperation) {
    if (enteredForOperation) exit4ByteAddressing();
}

uint8_t SpiNorFlash::readStatus1() {
    select();
    transfer(ReadStatus1);
    const uint8_t status = transfer(0xFF);
    deselect();
    return status;
}

bool SpiNorFlash::waitReady(uint32_t timeoutMs) {
    const uint32_t started = millis();
    while ((millis() - started) < timeoutMs) {
        if ((readStatus1() & StatusBusy) == 0) return true;
        delay(1);
    }
    _error = Busy;
    return false;
}

bool SpiNorFlash::enableWrite() {
    select();
    transfer(WriteEnable);
    deselect();
    if ((readStatus1() & StatusWriteEnable) == 0) {
        _error = WriteEnableFailed;
        return false;
    }
    return true;
}

uint32_t SpiNorFlash::capacityFromJedec(uint8_t capacityId) const {
    if (capacityId >= 0x10 && capacityId <= 0x20) {
        return 1UL << capacityId;
    }
    return 0;
}

bool SpiNorFlash::begin(uint32_t capacityBytes) {
    _spi.begin();
    _capacity = 0;
    _jedecId = 0;
    _address4Byte = false;
    _error = Success;

    select();
    transfer(ReadJedecId);
    const uint8_t manufacturer = transfer(0xFF);
    const uint8_t memoryType = transfer(0xFF);
    const uint8_t capacityId = transfer(0xFF);
    deselect();

    _jedecId = (static_cast<uint32_t>(manufacturer) << 16) |
               (static_cast<uint32_t>(memoryType) << 8) | capacityId;
    if (manufacturer == 0x00 || manufacturer == 0xFF) {
        _error = NoResponse;
        _begun = false;
        return false;
    }

    _capacity = capacityBytes != 0 ? capacityBytes : capacityFromJedec(capacityId);
    if (_capacity == 0) {
        _error = UnknownCapacity;
        _begun = false;
        return false;
    }

    _begun = true;
    return true;
}

uint8_t SpiNorFlash::error(bool verbose) const {
    if (verbose) {
        Serial.printf("SPI NOR error=%u jedec=0x%06lX capacity=%lu\n",
                      _error, static_cast<unsigned long>(_jedecId),
                      static_cast<unsigned long>(_capacity));
    }
    return _error;
}

uint32_t SpiNorFlash::getJEDECID() const {
    return _jedecId;
}

uint64_t SpiNorFlash::getUniqueID() {
    if (!_begun) {
        _error = CallBegin;
        return 0;
    }
    select();
    transfer(ReadUniqueId);
    for (uint8_t index = 0; index < 4; index++) transfer(0xFF);
    uint64_t uniqueId = 0;
    for (uint8_t index = 0; index < 8; index++) {
        uniqueId = (uniqueId << 8) | transfer(0xFF);
    }
    deselect();
    return uniqueId;
}

uint32_t SpiNorFlash::getCapacity() const {
    return _capacity;
}

uint32_t SpiNorFlash::getMaxPage() const {
    return _pageSize == 0 ? 0 : _capacity / _pageSize;
}

bool SpiNorFlash::validRange(uint32_t address, uint32_t length) const {
    return _begun && length <= _capacity && address <= (_capacity - length);
}

bool SpiNorFlash::readByteArray(uint32_t address, uint8_t* data, size_t length) {
    if (data == nullptr || length == 0 || length > UINT32_MAX ||
        !validRange(address, static_cast<uint32_t>(length))) {
        _error = OutOfBounds;
        return false;
    }

    const bool enteredForOperation = prepareAddress(address);
    select();
    transfer(ReadData);
    transferAddress(address);
    for (size_t index = 0; index < length; index++) data[index] = transfer(0xFF);
    deselect();
    finishAddress(enteredForOperation);
    _error = Success;
    return true;
}

uint8_t SpiNorFlash::readByte(uint32_t address) {
    uint8_t value = 0;
    if (!readByteArray(address, &value, 1)) return 0;
    return value;
}

bool SpiNorFlash::writePage(uint32_t address, const uint8_t* data, size_t length) {
    if (!enableWrite()) return false;
    const bool enteredForOperation = prepareAddress(address);
    select();
    transfer(PageProgram);
    transferAddress(address);
    for (size_t index = 0; index < length; index++) transfer(data[index]);
    deselect();
    const bool ready = waitReady();
    finishAddress(enteredForOperation);
    if (!ready) return false;
    _error = Success;
    return true;
}

bool SpiNorFlash::writeByteArray(uint32_t address, const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0 || length > UINT32_MAX ||
        !validRange(address, static_cast<uint32_t>(length))) {
        _error = OutOfBounds;
        return false;
    }

    size_t written = 0;
    while (written < length) {
        const uint32_t pageOffset = (address + written) % _pageSize;
        const size_t pageRemaining = _pageSize - pageOffset;
        const size_t chunk = min(pageRemaining, length - written);
        if (!writePage(address + written, data + written, chunk)) return false;
        written += chunk;
    }
    return true;
}

bool SpiNorFlash::writeByte(uint32_t address, uint8_t data) {
    return writeByteArray(address, &data, 1);
}

bool SpiNorFlash::eraseSector(uint32_t address) {
    if (!_begun || address >= _capacity || address % _sectorSize != 0) {
        _error = OutOfBounds;
        return false;
    }
    if (!enableWrite()) return false;
    const bool enteredForOperation = prepareAddress(address);
    select();
    transfer(SectorErase);
    transferAddress(address);
    deselect();
    const bool ready = waitReady();
    finishAddress(enteredForOperation);
    if (!ready) return false;
    _error = Success;
    return true;
}

bool SpiNorFlash::eraseSection(uint32_t address, uint32_t length) {
    if (length == 0 || address > _capacity || length > (_capacity - address)) {
        _error = OutOfBounds;
        return false;
    }
    const uint32_t start = address - (address % _sectorSize);
    const uint32_t end = address + length;
    const uint32_t alignedEnd = (end + _sectorSize - 1) / _sectorSize * _sectorSize;
    if (alignedEnd > _capacity) {
        _error = OutOfBounds;
        return false;
    }
    for (uint32_t sector = start; sector < alignedEnd; sector += _sectorSize) {
        if (!eraseSector(sector)) return false;
    }
    return true;
}