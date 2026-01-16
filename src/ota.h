#pragma once

#include <Arduino.h>
#include "xtp-lib.h"

// #include "OTAStorage.h"
#ifndef XTP_NO_BROADCAST
#define NOTA_BROADCAST
#endif // XTP_NO_BROADCAST
#include <NOTA.h>

#ifndef OTA_NAME
#define OTA_NAME "XTP"
#endif // OTA_NAME

#ifndef OTA_PORT
#define OTA_PORT 3232
#endif // OTA_PORT

#ifndef OTA_PASSWORD
#define OTA_PASSWORD 1234
#endif // OTA_PASSWORD

// MyFileStorageClass ota_storage;

// Define type for void function
typedef void (*void_function)(void);

void_function ota_notify = NULL;
void_function ota_shutdown = NULL;
void_function ota_resume = NULL;
constexpr const char* ota_password = ENV(OTA_PASSWORD);

bool ota_update_in_progress = false;

int prev_progress = 999;
void ota_setup() {

    uint16_t port = OTA_PORT;

    // ota_storage.init();
    // OTA.setStorage(ota_storage);
    OTA.setHostname(XTP_DEVICE_NAME);
    OTA.setPlatform("STM32");
    OTA.setPassword(ota_password);
    OTA.setPort(port);

    OTA.onRequest([]() {
        if (ota_notify) ota_notify();

        xtp_ssd1306_clear();
        xtp_ssd1306_setCursor(0, 0);
        xtp_ssd1306_print("OTA Firmware Update");
        oled_force_redraw = true;

        prev_progress = 999;
        String type = OTA.getCommand() == U_FLASH ? "flash" : "filesystem" /* U_SPIFFS */;
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("OTA update request: " + type);
        IWatchdog.reload();
        });
    OTA.onStart([]() {
        ota_update_in_progress = true;
        if (ota_shutdown) ota_shutdown();
        IWatchdog.reload();
        });
    OTA.onProgress([](unsigned int progress, unsigned int total) {
        IWatchdog.reload();
        float prog = 100.0 * (float) progress / (float) total;
        if (prev_progress != prog) {
            prev_progress = prog;
            Serial.printf("Progress: %6u/%6u (%3.1f%%)\n", progress, total, prog);
            char msg[12];
            sprintf(msg, "%3.1f%%", prog);
            xtp_ssd1306_setCursor(1, 2);  // Column 1, Row 2 (approx 6px, 16px)
            xtp_ssd1306_print(msg);
        }
        });
    OTA.onEnd([]() {
        IWatchdog.reload();
        xtp_ssd1306_setCursor(0, 5);  // Row 5 (approx 40px)
        xtp_ssd1306_print("Done - RESTARTING");
        Serial.println("\nEnd");
        delay(50);
        // NVIC_SystemReset();
        });
    OTA.onError([](ota_error_t error) {
        char msg[32];
        sprintf(msg, "Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) sprintf(msg, "%sAuth Failed", msg);
        else if (error == OTA_BEGIN_ERROR) sprintf(msg, "%sBegin Failed", msg);
        else if (error == OTA_CONNECT_ERROR) sprintf(msg, "%sConnect Failed", msg);
        else if (error == OTA_RECEIVE_ERROR) sprintf(msg, "%sReceive Failed", msg);
        else if (error == OTA_END_ERROR) sprintf(msg, "%sEnd Failed", msg);
        Serial.println(msg);

        xtp_ssd1306_setCursor(0, 5);  // Row 5 (approx 40px)
        xtp_ssd1306_print(msg);
        if (ota_update_in_progress) {
            ota_update_in_progress = false;
            thread_resume();
            xtp_ssd1306_clear();
            if (ota_resume) ota_resume();
        }
        });

    // #if not defined(REBOOT_ON_OTA_SUCCESS)
    //     OTA.setRebootOnSuccess(false);
    // #endif

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // OTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    OTA.begin();

    int32_t max_size = InternalStorage.maxSize();
    Serial.printf("OTA server started on port %u - max_size: %d\n", port, max_size);
}

void ota_reconnect() {
    OTA.reconnect(); // force re-init
}

void ota_loop() {
    OTA.handle();
}