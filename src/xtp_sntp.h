#pragma once

#include <Arduino.h>

#include "xtp_time.h"
#include "xtp_ethernet.h"

EthernetUDP sntp_client;
bool sntp_synchronized = false;

void sntp_sync(const char* server, uint16_t port = 123) {
    sntp_client.setTimeout(10000); // Set timeout to 1s
    sntp_client.begin(60000); // Start UDP client on port 123 (NTP) to listen for responses
    Serial.printf("Syncing time with SNTP server: %s:%d\n", server, port);
    int error = sntp_client.beginPacket(server, port);
    if (error != 1) {
        // Error
        Serial.printf("Error connecting to the SNTP server, error code: %d\n", error);
        return;
    }
    uint8_t packet[48] = { 0 };
    memset(packet, 0, 48); // Clear packet
    packet[0] = 0b11100011; // LI, VN, Mode
    Serial.printf("Sending SNTP request...\n");
    // Send request
    sntp_client.write(packet, 48);
    sntp_client.endPacket();
    // Wait for response
    uint32_t start = millis();
    while (millis() - start < 1000) {
        if (sntp_client.parsePacket() == 48) {
            sntp_client.read(packet, 48);
            break;
        }
    }
    if (millis() - start >= 1000) {
        // Timeout
        Serial.println("SNTP request timed out");
        return;
    }
    // Parse response
    if (packet[0] & 0b11000000) {
        uint32_t timestamp_s = 0;
        // Parse seconds
        uint32_t seconds = 0;
        for (int i = 40; i < 44; i++) {
            seconds = seconds << 8;
            seconds |= (uint32_t) packet[i];
        }
        uint32_t NTP_OFFSET = 2208988800;
        timestamp_s = seconds - NTP_OFFSET;
        Serial.printf("SNTP response received, seconds: %lu -> timestamp: %lu\n", seconds, timestamp_s);
        // Set time
        time_set_seconds(timestamp_s);
        char temp[50];
        time_print(temp);
        Serial.printf("Time set to: %s\n", temp);
    } else {
        // Error
        // TODO: Handle error
        Serial.println("Error parsing SNTP response");
    }
}