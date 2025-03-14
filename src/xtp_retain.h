#pragma once

#include <Arduino.h>
#include "xtp_config.h"

struct RetainedData_t {
    int32_t reboot_count;
    int32_t write_count;
    int32_t firmware_reset_count;
    struct Network_t {
        bool dhcp_enabled;
        uint8_t ip[4];
        uint8_t subnet[4];
        uint8_t gateway[4];
        uint8_t dns[4];
        uint32_t updated_ts;
    } network;
    char name[32];
};


RetainedData_t retainedData;
const RetainedData_t retainedDataDefault = {
    .reboot_count = 0,
    .write_count = 0,
    .firmware_reset_count = 0,
    .network = {
        .dhcp_enabled = true,
        .ip = { 192, 168, 1, 100 },
        .subnet = { 255, 255, 255, 0 },
        .gateway = { 192, 168, 1, 1 },
        .dns = { 8, 8, 8, 8 },
        .updated_ts = 0
    },
    // Add your default values here
};

constexpr int RETAINED_DATA_SIZE = sizeof(retainedDataDefault);