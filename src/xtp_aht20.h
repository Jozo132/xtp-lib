#pragma once

#include <Arduino.h>

#include "xtp_i2c.h"

#ifndef AHT20_MEASUREMENT_INTERVAL_MS
#define AHT20_MEASUREMENT_INTERVAL_MS 2000
#endif

#ifndef AHT20_I2C_TIMEOUT_MS
#define AHT20_I2C_TIMEOUT_MS 120
#endif

enum XtpAht20State {
  XTP_AHT20_IDLE,
  XTP_AHT20_INITIALIZING,
  XTP_AHT20_WAITING_FOR_INITIALIZATION,
  XTP_AHT20_WAITING_FOR_MEASUREMENT,
};

struct XtpAht20 {
  XtpAht20State state = XTP_AHT20_IDLE;
  I2CDevice* device = nullptr;
  uint32_t nextActionAt = 0;
  uint32_t measurementStartedAt = 0;
  uint32_t measurementIntervalMs = AHT20_MEASUREMENT_INTERVAL_MS;
  float temperatureC = 0.0f;
  float humidityPercent = 0.0f;
  uint32_t lastReadAt = 0;
  uint32_t errorCount = 0;
  bool hasReading = false;
  bool initialized = false;

  void setup() {
    if (initialized) return;
    initialized = true;
    device = i2cBus.findDevice(AHT20_I2C_ADDRESS);
    if (device == nullptr) {
      device = i2cBus.registerDevice(AHT20_I2C_ADDRESS, "AHT20", false);
    }
    nextActionAt = millis();
  }

  void update() {
    if (!initialized || device == nullptr) return;

    const uint32_t now = millis();
    if (!timeReached(now, nextActionAt)) return;

    switch (state) {
      case XTP_AHT20_IDLE:
        startInitialization(now);
        break;

      case XTP_AHT20_INITIALIZING:
        startMeasurement(now);
        break;

      case XTP_AHT20_WAITING_FOR_INITIALIZATION:
        state = XTP_AHT20_INITIALIZING;
        nextActionAt = now;
        break;

      case XTP_AHT20_WAITING_FOR_MEASUREMENT:
        readMeasurement(now);
        break;
    }
  }

  bool ready() const { return hasReading; }
  float temperature() const { return temperatureC; }
  float humidity() const { return humidityPercent; }
  uint32_t lastUpdate() const { return lastReadAt; }
  void setMeasurementInterval(uint32_t intervalMs) {
    measurementIntervalMs = intervalMs;
  }
  uint32_t measurementInterval() const { return measurementIntervalMs; }

 private:
  static bool timeReached(uint32_t now, uint32_t target) {
    return static_cast<int32_t>(now - target) >= 0;
  }

  void startInitialization(uint32_t now) {
    if (!i2c_device_probe(AHT20_I2C_ADDRESS)) {
      recordError(now);
      return;
    }

    const uint8_t command[] = {0xBE, 0x08, 0x00};
    if (!i2c_write(AHT20_I2C_ADDRESS, command, sizeof(command))) {
      recordError(now);
      return;
    }

    state = XTP_AHT20_WAITING_FOR_INITIALIZATION;
    nextActionAt = now + 10;
  }

  void startMeasurement(uint32_t now) {
    const uint8_t command[] = {0xAC, 0x33, 0x00};
    if (!i2c_write(AHT20_I2C_ADDRESS, command, sizeof(command))) {
      recordError(now);
      return;
    }

    state = XTP_AHT20_WAITING_FOR_MEASUREMENT;
    measurementStartedAt = now;
    nextActionAt = now + 10;
  }

  void readMeasurement(uint32_t now) {
    uint8_t data[7] = {};
    const size_t received = i2c_read(AHT20_I2C_ADDRESS, data, sizeof(data));

    if (received != sizeof(data)) {
      recordError(now);
      return;
    }

    if ((data[0] & 0x80) != 0) {
      if (now - measurementStartedAt < AHT20_I2C_TIMEOUT_MS) {
        nextActionAt = now + 5;
        return;
      }
      recordError(now);
      return;
    }

    if (crc8(data, 6) != data[6]) {
      recordError(now);
      return;
    }

    const uint32_t rawHumidity = (static_cast<uint32_t>(data[1]) << 12) |
                                  (static_cast<uint32_t>(data[2]) << 4) |
                                  (data[3] >> 4);
    const uint32_t rawTemperature =
        (static_cast<uint32_t>(data[3] & 0x0F) << 16) |
        (static_cast<uint32_t>(data[4]) << 8) | data[5];

    humidityPercent = (rawHumidity * 100.0f) / 1048576.0f;
    temperatureC = (rawTemperature * 200.0f) / 1048576.0f - 50.0f;
    lastReadAt = now;
    hasReading = true;
    state = XTP_AHT20_IDLE;
    nextActionAt = now + measurementIntervalMs;
  }

  void recordError(uint32_t now) {
    errorCount++;
    state = XTP_AHT20_IDLE;
    nextActionAt = now + measurementIntervalMs;
  }

  static uint8_t crc8(const uint8_t* data, size_t length) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8; bit++) {
        crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                           : static_cast<uint8_t>(crc << 1);
      }
    }
    return crc;
  }
};

XtpAht20 xtpAht20;

void aht20_setup() { xtpAht20.setup(); }
void aht20_loop() { xtpAht20.update(); }
bool aht20_has_reading() { return xtpAht20.ready(); }
float aht20_temperature_c() { return xtpAht20.temperature(); }
float aht20_humidity_percent() { return xtpAht20.humidity(); }
uint32_t aht20_last_update() { return xtpAht20.lastUpdate(); }
void aht20_set_measurement_interval(uint32_t intervalMs) {
  xtpAht20.setMeasurementInterval(intervalMs);
}
uint32_t aht20_measurement_interval() {
  return xtpAht20.measurementInterval();
}
