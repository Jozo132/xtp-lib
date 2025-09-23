#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUDP.h>
#include <utility/w5100.h>

#include "xtp_config.h"
#include "xtp_oled.h"
#include "xtp_spi.h"
#include "xtp_flash.h"

#ifdef UDP_RX_PACKET_MAX_SIZE
#undef UDP_RX_PACKET_MAX_SIZE
#define UDP_RX_PACKET_MAX_SIZE 2048
#else
#define UDP_RX_PACKET_MAX_SIZE 2048
#endif

#ifdef UDP_TX_PACKET_MAX_SIZE
#undef UDP_TX_PACKET_MAX_SIZE
#define UDP_TX_PACKET_MAX_SIZE 2048
#else
#define UDP_TX_PACKET_MAX_SIZE 2048
#endif

// buffers for receiving and sending data
char readBuffer[UDP_TX_PACKET_MAX_SIZE];        //buffer to hold incoming packet,
char writeBuffer[UDP_RX_PACKET_MAX_SIZE + 25];  // a string to send back

Timeout communication_idle(60000);

uint32_t ethernet_last_hard_reset = 0;
uint32_t ethernet_last_soft_reset = 0;

void w5500_hard_reset() {
    uint32_t t = millis();
    if (ethernet_last_hard_reset != 0 && t - ethernet_last_hard_reset < 60000) {
        return;
    }
    ethernet_last_hard_reset = t;
    sprintf(msg, "  HARD RESET   ");
    oled_print(msg, 1, 6);
    pinMode(ETH_RST_pin, OUTPUT);
    digitalWrite(ETH_RST_pin, LOW);
    delay(2);
    digitalWrite(ETH_RST_pin, HIGH);
    delay(1500);
    pinMode(ETH_RST_pin, INPUT);
}

void w5500_soft_reset() {
    uint32_t t = millis();
    if (ethernet_last_soft_reset != 0 && t - ethernet_last_soft_reset < 60000) {
        return;
    }
    ethernet_last_soft_reset = t;
    sprintf(msg, "  SOFT RESET   ");
    oled_print(msg, 1, 6);
    int count = 0;
    W5100.writeMR(0x80);                 // set RST bit
    while (W5100.readMR() & 0x80) {      // wait until cleared or timeout
        delay(1);
        count++;
        if (count > 1000) {
            break;
        }
    }
    delay(1500);
}


//EthernetUDP server; // UDP server port
EthernetServer server(local_port);  // TCP server port

// Ethernet TCP client for analytics
EthernetClient analytics_target;

uint32_t ethernet_cycle = 0;
bool ethernet_link_established = true;
int ethernet_link_status = 0;
bool ethernet_is_connected();
void ota_reconnect();
char ip_address[20];
char mac_address[18];

void update_ip_status() {
    // Print IP address local_ip with padEnd filled with spaces
    if (ethernet_link_status == LinkON) {
        sprintf(msg, "  %s", ip_address);
    } else if (ethernet_link_status == LinkOFF) {
        sprintf(msg, "   Disconnected");
    } else { // Unknown
        sprintf(msg, "      ??????   ");
    }
    int len = strlen(msg);
    for (len; len < 19; len++) {
        msg[len] = ' ';
    }
    msg[len] = 0;
    oled_print(msg, 1, 6);
}

int ethernet_anomaly_count_A = 0;
int ethernet_anomaly_count_B = 0;
void ethernet_init() {
    spi_select(SPI_None);
    ethernet_cycle++;
    Ethernet.init(ETH_CS_pin);
    bool status = ethernet_is_connected();
    if (status && ethernet_link_status == LinkOFF) {
        ethernet_anomaly_count_A++;
        if (ethernet_anomaly_count_A >= 3000) {
            ethernet_anomaly_count_A = 0;
            Serial.println("Ethernet link anomaly detected, performing hard reset");
            w5500_hard_reset();
            delay(1000);
            status = ethernet_is_connected();
        } else {
            ethernet_anomaly_count_A = 0;
        }
    }
    if (local_ip[0] == 0 && local_ip[1] == 0 && local_ip[2] == 0 && local_ip[3] == 0 && ethernet_link_established) {
        ethernet_anomaly_count_B++;
        if (ethernet_anomaly_count_B >= 3000) {
            ethernet_anomaly_count_B = 0;
            ethernet_link_established = false;
            w5500_hard_reset();
            delay(1000);
            ethernet_is_connected();
            update_ip_status();
            return;
        } else {
            ethernet_anomaly_count_B = 0;
        }
    }
    if (!status) {
        w5500_hard_reset();
        ethernet_link_established = false;
        if (ethernet_cycle == 1) displayMsg("                  ");
        update_ip_status();
        return;
    }
    spi_select(SPI_Ethernet);

    ethernet_link_established = true;
    Serial.println("Ethernet link is ON");

    auto& network = retainedData.network;

    bool DHCP_enabled = network.dhcp_enabled;

    if (DHCP_enabled) {
        Ethernet.begin(local_mac);
    } else {
        Ethernet.begin(local_mac, network.ip, network.dns, network.gateway, network.subnet);
    }

    IPAddress myIP = Ethernet.localIP();
    if (!DHCP_enabled && myIP[0] == 0) {
        Serial.printf("Failed to assign static IP address %d.%d.%d.%d\n", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
        network.dhcp_enabled = true;
        Serial.println("Switching to DHCP");
        flash_store_retained_data();
        ethernet_init();
        update_ip_status();
        return;
    }

    local_ip[0] = myIP[0];
    local_ip[1] = myIP[1];
    local_ip[2] = myIP[2];
    local_ip[3] = myIP[3];
    if (DHCP_enabled) {
        network.ip[0] = local_ip[0];
        network.ip[1] = local_ip[1];
        network.ip[2] = local_ip[2];
        network.ip[3] = local_ip[3];
        IPAddress subnet = Ethernet.subnetMask();
        network.subnet[0] = subnet[0];
        network.subnet[1] = subnet[1];
        network.subnet[2] = subnet[2];
        network.subnet[3] = subnet[3];
        IPAddress gateway = Ethernet.gatewayIP();
        network.gateway[0] = gateway[0];
        network.gateway[1] = gateway[1];
        network.gateway[2] = gateway[2];
        network.gateway[3] = gateway[3];
        IPAddress dns = Ethernet.dnsServerIP();
        network.dns[0] = dns[0];
        network.dns[1] = dns[1];
        network.dns[2] = dns[2];
        network.dns[3] = dns[3];
        flash_store_retained_data();
    }
    sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);

    spi_select(SPI_Ethernet);
    delay(10);
    server.begin();
    delay(10);
    update_ip_status();
    ota_reconnect();
}

bool ethernet_is_connected() {
    spi_select(SPI_Ethernet);
    ethernet_link_status = Ethernet.linkStatus();
    sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    if (ethernet_link_status != LinkON) {
        if (ethernet_link_established) {
            Serial.println("Ethernet link is OFF");
            ethernet_link_established = false;
            IPAddress myIP = Ethernet.localIP();
            local_ip[0] = 0;
            local_ip[1] = 0;
            local_ip[2] = 0;
            local_ip[3] = 0;
            sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
        }
        return false;
    }
    return true;
}

void ethernet_setup() {
    oled_print("Starting up ... ", 0, 0);
    // getDeviceUUID(); // Get 12 byte UUID array

    local_mac[0] = 0x1E;
#if defined(XTP_12A6_E)
    local_mac[1] = 0x12;
#elif defined(XTP_14A6_E)
    local_mac[1] = 0x14;
#else
    local_mac[1] = 0x69;
#endif
    local_mac[2] = DEVICE_UID[0];
    local_mac[3] = DEVICE_UID[1];
    local_mac[4] = DEVICE_UID[2];
    local_mac[5] = DEVICE_UID[3];

    sprintf(DEFAULT_DEVICE_NAME, "%s-%02X%02X%02X%02X%02X", DEVICE_NAME, local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);

    char msg[100];
    sprintf(mac_address, "%02x:%02x:%02x:%02x:%02x:%02x", local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);
    sprintf(msg, " %s", mac_address);
    IWatchdog.reload();
    oled_print(msg, 0, 7);
    delay(200);
    IWatchdog.reload();
    ethernet_init();
    oled_print("                ", 0, 0); // Clear the first line
    IWatchdog.reload();
    communication_idle.set(60000);
}

// void UDP_end() {
//     UDP.endPacket();
// }

void TCP_end(EthernetClient client) {
    //delay(1);
    client.stop();  //stopping client
}

// void UDP_read() {
//     server.read(readBuffer, UDP_TX_PACKET_MAX_SIZE);  // read the packet into readBuffer
// }

void TCP_read(EthernetClient client) {
    int index = 0;
    while (client.available()) {
        readBuffer[index] = client.read();
        index++;
    }
    readBuffer[index] = '\0';
}

void TCP_send(EthernetClient client, char* message) { client.println(message); }

// void sendMessage(IPAddress host, uint16_t port, char* message) {
//     digitalWrite(LED_BUILTIN, LOW);
//     server.beginPacket(host, port);
//     server.write(message);
//     server.endPacket();
//     delay(1);
//     digitalWrite(LED_BUILTIN, HIGH);
//     IWatchdog.reload();
// }
// void sendMessage(const char* host, uint16_t port, char* message) {
//     digitalWrite(LED_BUILTIN, LOW);
//     server.beginPacket(host, port);
//     server.write(message);
//     server.endPacket();
//     delay(1);
//     digitalWrite(LED_BUILTIN, HIGH);
//     IWatchdog.reload();
// }
bool sendMessage(IPAddress host, uint16_t port, char* message) {
    communication_idle.reset();
    // digitalWrite(LED_BUILTIN, LOW);
    spi_select(SPI_Ethernet);
    EthernetClient client;
    if (client.connect(host, port)) {
        TCP_send(client, message);
        TCP_end(client);
        // digitalWrite(LED_BUILTIN, HIGH);
        IWatchdog.reload();
        spi_select(SPI_None);
        return true;
    } else {
        ethernet_init();
        // digitalWrite(LED_BUILTIN, HIGH);
        IWatchdog.reload();
        spi_select(SPI_None);
        return false;
    }
}
bool sendMessage(const char* host, uint16_t port, char* message) {
    communication_idle.reset();
    // digitalWrite(LED_BUILTIN, LOW);
    spi_select(SPI_Ethernet);
    EthernetClient client;
    if (client.connect(host, port)) {
        TCP_send(client, message);
        TCP_end(client);
        // digitalWrite(LED_BUILTIN, HIGH);
        IWatchdog.reload();
        spi_select(SPI_None);
        return true;
    } else {
        ethernet_init();
        // digitalWrite(LED_BUILTIN, HIGH);
        IWatchdog.reload();
        spi_select(SPI_None);
        return false;
    }
}

TON ip_null_timeout(60000);
uint32_t ethernet_loop_time = 0;
int link_toggle_times[4] = { 0, 0, 0, 0 };
constexpr int max_link_toggles = sizeof(link_toggle_times) / sizeof(link_toggle_times[0]);
int link_toggle_index = 0;
bool link_toggle_state = false;
uint32_t link_toggle_time = 0;

void ethernet_loop() {
    spi_select(SPI_Ethernet);

    uint32_t t = millis();
    int32_t dt = t - ethernet_loop_time;
    if (dt < 1) dt = 1;
    ethernet_loop_time = t;

    // Try to re-establish ethernet connection if it is lost
    bool connected = ethernet_is_connected();
    bool connection_restored = connected && !ethernet_link_established;

    bool undefined_state = ethernet_link_status == Unknown;

    bool ip_is_null = local_ip[0] == 0 && local_ip[1] == 0 && local_ip[2] == 0 && local_ip[3] == 0;

    bool ip_is_null_for_too_long = ip_null_timeout.update(ip_is_null, dt);

    // uint8_t w5500_reg = W5100.readPHYCFGR_W5500();
    // bool link_led = (w5500_reg & 0x01) != 0;
    bool link_led = ethernet_link_status == LinkON;

    if (link_led != link_toggle_state) {
        link_toggle_state = link_led;
        link_toggle_times[link_toggle_index] = t;
        link_toggle_index = (link_toggle_index + 1) % max_link_toggles;
    }
    if (link_toggle_index == 0) {
        // If the duration between each pair of toggles is less than 5 seconds, do a hard reset
        bool too_many_toggles = true;
        for (int i = 0; i < max_link_toggles - 2; i++) {
            int j = i + 1;
            int diff = link_toggle_times[j] - link_toggle_times[i];
            if (diff > 5000) {
                too_many_toggles = false;
                break;
            }
        }
        if (too_many_toggles) {
            w5500_hard_reset();
        }
    }


    if (connection_restored || undefined_state || ip_is_null_for_too_long) {
        ethernet_init();
    } else {
        update_ip_status();
    }

    if (connected) Ethernet.maintain();

    spi_select(SPI_None);
}