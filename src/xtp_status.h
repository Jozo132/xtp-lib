#pragma once

#include <Arduino.h>
#include <IWatchdog.h>
#include <stdarg.h>
#include <string.h>

#if defined(XTP_3)
#include "xtp_ethernet_lwip.h"
#endif

#ifndef XTP_STATUS_TCP_PORT
#define XTP_STATUS_TCP_PORT 3233
#endif

using XtpStatusSink = void (*)(void* context, const char* text);

void xtp_status_emit(XtpStatusSink sink, void* context, const char* format, ...) {
    char line[192] = {};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    sink(context, line);
    IWatchdog.reload();
}

void xtp_status_serial_sink(void*, const char* text) {
    Serial.print(text);
}

void xtp_status_report(XtpStatusSink sink, void* context) {
    xtp_status_emit(sink, context, "XTP status\n");
    xtp_status_emit(sink, context, "device=%s uptime_ms=%lu\n", XTP_DEVICE_NAME,
                    static_cast<unsigned long>(millis()));

    IWatchdog.reload();
    const i2c_scan_result_t scan = i2c_scan();
    IWatchdog.reload();
    xtp_status_emit(sink, context,
                    "I2C bus=Wire initialized=%s errors=%lu transactions=%lu found=%u\n",
                    i2cBus.initialized ? "yes" : "no",
                    static_cast<unsigned long>(i2cBus.busErrorCount),
                    static_cast<unsigned long>(i2cBus.totalTransactions), scan.count);
    for (uint8_t index = 0; index < scan.count; index++) {
        const uint8_t address = scan.addresses[index];
        I2CDevice* device = i2cBus.findDevice(address);
        xtp_status_emit(sink, context, "  I2C target=0x%02X name=%s state=%s\n", address,
                        device && device->name ? device->name : "unknown",
                        device && device->isPresent() ? "present" : "discovered");
    }

#if defined(XTP_3)
    xtp_status_emit(sink, context,
                    "SPI2 target=BY25FQ256ESSIG bus=SPI2 CS=PA8 SCK=PA9 MISO=PC2 MOSI=PC3\n");
#if defined(NOTA_EXT_FLASH_OTA) || defined(XTP3_ENABLE_LOCAL_HARDWARE)
    xtp_status_emit(sink, context,
                    "  flash initialized=%s error=%u jedec=0x%06lX capacity=%lu\n",
                    flash_initialized ? "yes" : "no", flash.error(),
                    static_cast<unsigned long>(flash.getJEDECID()),
                    static_cast<unsigned long>(flash.getCapacity()));
#endif
    xtp_status_emit(sink, context,
                    "SPI3 target=LSR8IO bus=SPI3 SCK=PB3 MISO=PB4 MOSI=PD6 LATCH=PD7 ENABLE=PD5 probe=unsupported\n");
    xtp_status_emit(sink, context,
                    "SPI4 target=AUX expansion bus=SPI4 SCK=PE2 MISO=PE5 MOSI=PE6 CS=PE3 probe=not_performed\n");
#else
    xtp_status_emit(sink, context, "SPI flash target=configured probe=not_performed\n");
#endif

    char oled_status[256] = {};
    oled_status_json(oled_status, sizeof(oled_status));
    xtp_status_emit(sink, context, "OLED %s\n", oled_status);
    xtp_status_emit(sink, context, "Ethernet state=%s ready=%s link=%s server=%s ip=%s mac=%s\n",
                    ethernet_state_name(), ethernet_ready() ? "yes" : "no",
                    ethState.linkEstablished ? "up" : "down",
                    ethState.serverReady ? "ready" : "not_ready", ip_address, mac_address);
    xtp_status_emit(sink, context, "UART command=get status\n");
    xtp_status_emit(sink, context, "END status\n");
}

char xtp_status_uart_line[48] = {};
uint8_t xtp_status_uart_length = 0;

void xtp_status_uart_loop() {
    while (Serial.available() > 0) {
        const char value = static_cast<char>(Serial.read());
        if (value == '\r' || value == '\n') {
            xtp_status_uart_line[xtp_status_uart_length] = '\0';
            if (strcmp(xtp_status_uart_line, "get status") == 0 ||
                strcmp(xtp_status_uart_line, "status") == 0) {
                xtp_status_report(xtp_status_serial_sink, nullptr);
            } else if (xtp_status_uart_length > 0) {
                Serial.println("ERR unknown command; use: get status");
            }
            xtp_status_uart_length = 0;
        } else if (xtp_status_uart_length < sizeof(xtp_status_uart_line) - 1) {
            xtp_status_uart_line[xtp_status_uart_length++] = value;
        }
    }
}

#if defined(XTP_3)
xtp_lwip::TcpServer xtp_status_tcp_server;
xtp_lwip::TcpClient* xtp_status_tcp_client = nullptr;
bool xtp_status_tcp_started = false;
char xtp_status_tcp_line[48] = {};
uint8_t xtp_status_tcp_length = 0;

void xtp_status_tcp_sink(void* context, const char* text) {
    auto* client = static_cast<xtp_lwip::TcpClient*>(context);
    if (client != nullptr) client->write(text);
}

void xtp_status_tcp_loop() {
    if (!ethernet_ready()) return;
    if (!xtp_status_tcp_started) {
        xtp_status_tcp_started = xtp_status_tcp_server.begin(XTP_STATUS_TCP_PORT);
        if (xtp_status_tcp_started) {
            Serial.printf("Status TCP server at port %u\n", XTP_STATUS_TCP_PORT);
        }
        return;
    }

    xtp_lwip::TcpClient* client = xtp_status_tcp_server.available();
    if (client != nullptr && xtp_status_tcp_client == nullptr) {
        xtp_status_tcp_client = client;
        xtp_status_tcp_length = 0;
    }
    if (xtp_status_tcp_client == nullptr) return;

    while (xtp_status_tcp_client->available() > 0) {
        const char value = static_cast<char>(xtp_status_tcp_client->read());
        if (value == '\r' || value == '\n') {
            xtp_status_tcp_line[xtp_status_tcp_length] = '\0';
            if (strcmp(xtp_status_tcp_line, "get status") == 0 ||
                strcmp(xtp_status_tcp_line, "status") == 0) {
                xtp_status_report(xtp_status_tcp_sink, xtp_status_tcp_client);
                xtp_status_tcp_client->flush();
                xtp_status_tcp_client = nullptr;
            } else if (xtp_status_tcp_length > 0) {
                xtp_status_tcp_client->print("ERR unknown command; use: get status\n");
            }
            xtp_status_tcp_length = 0;
        } else if (xtp_status_tcp_length < sizeof(xtp_status_tcp_line) - 1) {
            xtp_status_tcp_line[xtp_status_tcp_length++] = value;
        }
    }
}
#else
void xtp_status_tcp_loop() {}
#endif

void xtp_status_setup() {}

void xtp_status_loop() {
    xtp_status_uart_loop();
    xtp_status_tcp_loop();
}