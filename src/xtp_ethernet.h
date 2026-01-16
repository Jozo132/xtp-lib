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

// ============================================================================
// Non-blocking Ethernet State Machine
// ============================================================================

enum EthernetState {
    ETH_STATE_IDLE,                  // Normal operation, network is ready
    ETH_STATE_HARD_RESET_START,      // Begin hard reset sequence
    ETH_STATE_HARD_RESET_LOW,        // RST pin held low
    ETH_STATE_HARD_RESET_WAIT,       // Waiting after RST pin released
    ETH_STATE_SOFT_RESET_START,      // Begin soft reset
    ETH_STATE_SOFT_RESET_WAIT,       // Waiting for soft reset to complete
    ETH_STATE_INIT_START,            // Begin initialization
    ETH_STATE_INIT_CHECK_LINK,       // Check link status
    ETH_STATE_INIT_DHCP,             // DHCP in progress (can be slow)
    ETH_STATE_INIT_STATIC,           // Static IP configuration
    ETH_STATE_INIT_SERVER_START,     // Start server
    ETH_STATE_INIT_COMPLETE,         // Initialization complete
    ETH_STATE_DISCONNECTED,          // Link is down
    ETH_STATE_ERROR                  // Error state, will retry
};

struct EthernetStateMachine {
    EthernetState state = ETH_STATE_INIT_START;
    EthernetState previousState = ETH_STATE_IDLE;
    uint32_t stateEnteredAt = 0;
    uint32_t lastHardReset = 0;
    uint32_t lastSoftReset = 0;
    uint32_t softResetPollCount = 0;
    bool initialized = false;
    bool linkEstablished = false;
    bool serverReady = false;
    bool dhcpInProgress = false;
    uint8_t retryCount = 0;
    uint8_t initCycle = 0;
    
    // Timing constants (in milliseconds)
    static constexpr uint32_t HARD_RESET_LOW_TIME = 5;
    static constexpr uint32_t HARD_RESET_WAIT_TIME = 2500;
    static constexpr uint32_t SOFT_RESET_WAIT_TIME = 100;
    static constexpr uint32_t SOFT_RESET_TIMEOUT = 1000;
    static constexpr uint32_t SERVER_START_DELAY = 10;
    static constexpr uint32_t MIN_HARD_RESET_INTERVAL = 90000;
    static constexpr uint32_t MIN_SOFT_RESET_INTERVAL = 45000;
    static constexpr uint32_t DHCP_TIMEOUT = 30000;
    static constexpr uint32_t ERROR_RETRY_DELAY = 5000;
    static constexpr uint32_t LINK_CHECK_INTERVAL = 1000;
    
    void enterState(EthernetState newState) {
        if (state != newState) {
            previousState = state;
            state = newState;
            stateEnteredAt = millis();
        }
    }
    
    uint32_t timeInState() const {
        return millis() - stateEnteredAt;
    }
    
    bool isReady() const {
        return state == ETH_STATE_IDLE && linkEstablished && serverReady;
    }
    
    bool isBusy() const {
        return state != ETH_STATE_IDLE && state != ETH_STATE_DISCONNECTED && state != ETH_STATE_ERROR;
    }
    
    const char* getStateName() const {
        switch (state) {
            case ETH_STATE_IDLE: return "IDLE";
            case ETH_STATE_HARD_RESET_START: return "HARD_RESET_START";
            case ETH_STATE_HARD_RESET_LOW: return "HARD_RESET_LOW";
            case ETH_STATE_HARD_RESET_WAIT: return "HARD_RESET_WAIT";
            case ETH_STATE_SOFT_RESET_START: return "SOFT_RESET_START";
            case ETH_STATE_SOFT_RESET_WAIT: return "SOFT_RESET_WAIT";
            case ETH_STATE_INIT_START: return "INIT_START";
            case ETH_STATE_INIT_CHECK_LINK: return "INIT_CHECK_LINK";
            case ETH_STATE_INIT_DHCP: return "INIT_DHCP";
            case ETH_STATE_INIT_STATIC: return "INIT_STATIC";
            case ETH_STATE_INIT_SERVER_START: return "INIT_SERVER_START";
            case ETH_STATE_INIT_COMPLETE: return "INIT_COMPLETE";
            case ETH_STATE_DISCONNECTED: return "DISCONNECTED";
            case ETH_STATE_ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};

EthernetStateMachine ethState;

// Forward declarations
void ota_reconnect();
bool ethernet_is_connected();

char ip_address[20] = "0.0.0.0";
char mac_address[18] = "";

//EthernetUDP server; // UDP server port
EthernetServer server(local_port);  // TCP server port

// Ethernet TCP client for analytics
EthernetClient analytics_target;

// ============================================================================
// Display Helpers
// ============================================================================

void update_ip_status() {
    if (ethState.linkEstablished && ethState.serverReady) {
        sprintf(msg, "  %s", ip_address);
    } else if (ethState.state == ETH_STATE_DISCONNECTED) {
        sprintf(msg, "   Disconnected");
    } else if (ethState.isBusy()) {
        sprintf(msg, "  Connecting...");
    } else {
        sprintf(msg, "      ??????   ");
    }
    int len = strlen(msg);
    for (; len < 19; len++) {
        msg[len] = ' ';
    }
    msg[len] = 0;
    oled_print(msg, 1, 6);
}

void display_state_msg(const char* message) {
    sprintf(msg, "  %-13s", message);
    oled_print(msg, 1, 6);
}

// ============================================================================
// Non-blocking Reset Functions
// ============================================================================

// Request a hard reset (non-blocking, will be processed in state machine)
void eth_request_hard_reset(bool force = false) {
    uint32_t t = millis();
    uint32_t elapsed = t - ethState.lastHardReset;
    if (elapsed >= EthernetStateMachine::MIN_HARD_RESET_INTERVAL || force) {
        Serial.println("[ETH] Hard reset requested");
        ethState.enterState(ETH_STATE_HARD_RESET_START);
    }
}

// Request a soft reset (non-blocking)
void eth_request_soft_reset(bool force = false) {
    uint32_t t = millis();
    uint32_t elapsed = t - ethState.lastSoftReset;
    if (elapsed >= EthernetStateMachine::MIN_SOFT_RESET_INTERVAL || force) {
        Serial.println("[ETH] Soft reset requested");
        ethState.enterState(ETH_STATE_SOFT_RESET_START);
    }
}

// Request reconnection
void eth_request_reconnect() {
    if (!ethState.isBusy()) {
        Serial.println("[ETH] Reconnect requested");
        ethState.enterState(ETH_STATE_INIT_START);
    }
}

// ============================================================================
// State Machine Update (call from loop)
// ============================================================================

void ethernet_state_machine_update() {
    spi_select(SPI_Ethernet);
    
    uint32_t now = millis();
    uint32_t timeInState = ethState.timeInState();
    
    switch (ethState.state) {
        
        // -----------------------------------------------------------------
        // IDLE - Normal operation
        // -----------------------------------------------------------------
        case ETH_STATE_IDLE: {
            // Periodic link check
            static uint32_t lastLinkCheck = 0;
            if (now - lastLinkCheck >= EthernetStateMachine::LINK_CHECK_INTERVAL) {
                lastLinkCheck = now;
                
                spi_select(SPI_Ethernet);
                int linkStatus = Ethernet.linkStatus();
                
                if (linkStatus != LinkON) {
                    Serial.println("[ETH] Link lost");
                    ethState.linkEstablished = false;
                    ethState.serverReady = false;
                    ethState.enterState(ETH_STATE_DISCONNECTED);
                } else {
                    // Maintain DHCP lease
                    Ethernet.maintain();
                }
            }
            break;
        }
        
        // -----------------------------------------------------------------
        // HARD RESET sequence
        // -----------------------------------------------------------------
        case ETH_STATE_HARD_RESET_START: {
            display_state_msg("HARD RESET");
            Serial.println("[ETH] Starting hard reset");
            ethState.lastHardReset = now;
            ethState.linkEstablished = false;
            ethState.serverReady = false;
            
            // Pull reset pin low
            digitalWrite(ETH_RST_pin, LOW);
            ethState.enterState(ETH_STATE_HARD_RESET_LOW);
            break;
        }
        
        case ETH_STATE_HARD_RESET_LOW: {
            if (timeInState >= EthernetStateMachine::HARD_RESET_LOW_TIME) {
                // Release reset pin
                digitalWrite(ETH_RST_pin, HIGH);
                ethState.enterState(ETH_STATE_HARD_RESET_WAIT);
            }
            break;
        }
        
        case ETH_STATE_HARD_RESET_WAIT: {
            if (timeInState >= EthernetStateMachine::HARD_RESET_WAIT_TIME) {
                // After hard reset, do a soft reset
                ethState.enterState(ETH_STATE_SOFT_RESET_START);
            }
            break;
        }
        
        // -----------------------------------------------------------------
        // SOFT RESET sequence
        // -----------------------------------------------------------------
        case ETH_STATE_SOFT_RESET_START: {
            display_state_msg("SOFT RESET");
            Serial.println("[ETH] Starting soft reset");
            ethState.lastSoftReset = now;
            ethState.softResetPollCount = 0;
            
            // Set RST bit in Mode Register
            W5100.writeMR(0x80);
            ethState.enterState(ETH_STATE_SOFT_RESET_WAIT);
            break;
        }
        
        case ETH_STATE_SOFT_RESET_WAIT: {
            // Check if reset bit is cleared (reset complete)
            uint8_t mr = W5100.readMR();
            if ((mr & 0x80) == 0) {
                // Soft reset complete
                Serial.println("[ETH] Soft reset complete");
                ethState.enterState(ETH_STATE_INIT_START);
            } else if (timeInState >= EthernetStateMachine::SOFT_RESET_TIMEOUT) {
                // Timeout waiting for soft reset
                Serial.println("[ETH] Soft reset timeout");
                ethState.enterState(ETH_STATE_ERROR);
            }
            // Non-blocking poll
            break;
        }
        
        // -----------------------------------------------------------------
        // INITIALIZATION sequence
        // -----------------------------------------------------------------
        case ETH_STATE_INIT_START: {
            display_state_msg("INIT");
            Serial.println("[ETH] Starting initialization");
            ethState.initCycle++;
            
            spi_select(SPI_None);
            Ethernet.init(ETH_CS_pin);
            spi_select(SPI_Ethernet);
            
            ethState.enterState(ETH_STATE_INIT_CHECK_LINK);
            break;
        }
        
        case ETH_STATE_INIT_CHECK_LINK: {
            int linkStatus = Ethernet.linkStatus();
            
            if (linkStatus == LinkON) {
                Serial.println("[ETH] Link is ON");
                ethState.linkEstablished = true;
                
                auto& network = retainedData.network;
                if (network.dhcp_enabled) {
                    ethState.enterState(ETH_STATE_INIT_DHCP);
                } else {
                    ethState.enterState(ETH_STATE_INIT_STATIC);
                }
            } else if (linkStatus == LinkOFF) {
                Serial.println("[ETH] Link is OFF");
                ethState.linkEstablished = false;
                ethState.enterState(ETH_STATE_DISCONNECTED);
            } else {
                // Unknown state - wait a bit then retry or hard reset
                if (timeInState > 5000) {
                    Serial.println("[ETH] Link status unknown, requesting hard reset");
                    eth_request_hard_reset(true);
                }
            }
            break;
        }
        
        case ETH_STATE_INIT_DHCP: {
            if (!ethState.dhcpInProgress) {
                display_state_msg("DHCP");
                Serial.println("[ETH] Starting DHCP");
                ethState.dhcpInProgress = true;
                
                // Note: Ethernet.begin() with just MAC does DHCP - this CAN block
                // For truly non-blocking DHCP, you'd need to modify the Ethernet library
                // or use a different approach. For now, we'll do it here but with a timeout.
                int result = Ethernet.begin(local_mac);
                ethState.dhcpInProgress = false;
                
                if (result == 0) {
                    Serial.println("[ETH] DHCP failed");
                    ethState.retryCount++;
                    if (ethState.retryCount >= 3) {
                        // Switch to static IP as fallback
                        retainedData.network.dhcp_enabled = false;
                        flash_store_retained_data();
                        Serial.println("[ETH] Falling back to static IP");
                    }
                    ethState.enterState(ETH_STATE_ERROR);
                } else {
                    // DHCP success - update IP info
                    IPAddress myIP = Ethernet.localIP();
                    local_ip[0] = myIP[0];
                    local_ip[1] = myIP[1];
                    local_ip[2] = myIP[2];
                    local_ip[3] = myIP[3];
                    
                    // Store network config
                    auto& network = retainedData.network;
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
                    
                    sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
                    Serial.printf("[ETH] DHCP assigned IP: %s\n", ip_address);
                    
                    ethState.retryCount = 0;
                    ethState.enterState(ETH_STATE_INIT_SERVER_START);
                }
            }
            break;
        }
        
        case ETH_STATE_INIT_STATIC: {
            display_state_msg("STATIC IP");
            Serial.println("[ETH] Configuring static IP");
            
            auto& network = retainedData.network;
            Ethernet.begin(local_mac, network.ip, network.dns, network.gateway, network.subnet);
            
            IPAddress myIP = Ethernet.localIP();
            if (myIP[0] == 0) {
                Serial.println("[ETH] Static IP configuration failed");
                // Fall back to DHCP
                network.dhcp_enabled = true;
                flash_store_retained_data();
                ethState.enterState(ETH_STATE_INIT_DHCP);
            } else {
                local_ip[0] = myIP[0];
                local_ip[1] = myIP[1];
                local_ip[2] = myIP[2];
                local_ip[3] = myIP[3];
                sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
                Serial.printf("[ETH] Static IP configured: %s\n", ip_address);
                
                ethState.enterState(ETH_STATE_INIT_SERVER_START);
            }
            break;
        }
        
        case ETH_STATE_INIT_SERVER_START: {
            if (timeInState >= EthernetStateMachine::SERVER_START_DELAY) {
                display_state_msg("SERVER START");
                Serial.println("[ETH] Starting server");
                
                spi_select(SPI_Ethernet);
                server.begin();
                
                ethState.serverReady = true;
                ethState.enterState(ETH_STATE_INIT_COMPLETE);
            }
            break;
        }
        
        case ETH_STATE_INIT_COMPLETE: {
            Serial.printf("[ETH] Initialization complete - IP: %s\n", ip_address);
            update_ip_status();
            
            // Reconnect OTA if this isn't the first init
            if (ethState.initCycle > 1) {
                ota_reconnect();
            }
            
            ethState.enterState(ETH_STATE_IDLE);
            break;
        }
        
        // -----------------------------------------------------------------
        // DISCONNECTED - Link is down
        // -----------------------------------------------------------------
        case ETH_STATE_DISCONNECTED: {
            static uint32_t lastReconnectAttempt = 0;
            
            // Update display
            if (timeInState < 100) {
                display_state_msg("DISCONNECTED");
                local_ip[0] = 0;
                local_ip[1] = 0;
                local_ip[2] = 0;
                local_ip[3] = 0;
                sprintf(ip_address, "0.0.0.0");
            }
            
            // Periodically check if link is back
            if (now - lastReconnectAttempt >= 2000) {
                lastReconnectAttempt = now;
                
                spi_select(SPI_Ethernet);
                int linkStatus = Ethernet.linkStatus();
                
                if (linkStatus == LinkON) {
                    Serial.println("[ETH] Link restored");
                    ethState.enterState(ETH_STATE_INIT_START);
                }
            }
            break;
        }
        
        // -----------------------------------------------------------------
        // ERROR - Something went wrong, retry after delay
        // -----------------------------------------------------------------
        case ETH_STATE_ERROR: {
            if (timeInState < 100) {
                display_state_msg("ERROR");
                Serial.println("[ETH] Error state, will retry");
            }
            
            if (timeInState >= EthernetStateMachine::ERROR_RETRY_DELAY) {
                ethState.retryCount++;
                if (ethState.retryCount >= 5) {
                    // Too many retries, do a hard reset
                    ethState.retryCount = 0;
                    eth_request_hard_reset(true);
                } else {
                    ethState.enterState(ETH_STATE_INIT_START);
                }
            }
            break;
        }
    }
    
    spi_select(SPI_None);
}

// ============================================================================
// Legacy API (for backward compatibility)
// ============================================================================

uint32_t ethernet_last_hard_reset = 0;  // Kept for compatibility
uint32_t ethernet_last_soft_reset = 0;  // Kept for compatibility

// Blocking soft reset (legacy) - prefer eth_request_soft_reset()
void w5500_soft_reset(bool force = false) {
    eth_request_soft_reset(force);
    // Wait for completion (blocking for legacy compatibility)
    uint32_t timeout = millis() + 2000;
    while (ethState.state == ETH_STATE_SOFT_RESET_START || 
           ethState.state == ETH_STATE_SOFT_RESET_WAIT) {
        ethernet_state_machine_update();
        IWatchdog.reload();
        if (millis() > timeout) break;
    }
}

// Blocking hard reset (legacy) - prefer eth_request_hard_reset()
void w5500_hard_reset(bool force = false) {
    eth_request_hard_reset(force);
    // Wait for completion (blocking for legacy compatibility)
    uint32_t timeout = millis() + 5000;
    while (ethState.state == ETH_STATE_HARD_RESET_START || 
           ethState.state == ETH_STATE_HARD_RESET_LOW ||
           ethState.state == ETH_STATE_HARD_RESET_WAIT) {
        ethernet_state_machine_update();
        IWatchdog.reload();
        if (millis() > timeout) break;
    }
}

uint32_t ethernet_cycle = 0;
bool ethernet_link_established = true;  // Legacy - use ethState.linkEstablished
int ethernet_link_status = 0;

bool ethernet_is_connected() {
    spi_select(SPI_Ethernet);
    ethernet_link_status = Ethernet.linkStatus();
    sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    
    if (ethernet_link_status != LinkON) {
        if (ethernet_link_established) {
            Serial.println("Ethernet link is OFF");
            ethernet_link_established = false;
            local_ip[0] = 0;
            local_ip[1] = 0;
            local_ip[2] = 0;
            local_ip[3] = 0;
            sprintf(ip_address, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
        }
        return false;
    }
    ethernet_link_established = true;
    return true;
}

// Legacy ethernet_init - now just triggers state machine
void ethernet_init() {
    eth_request_reconnect();
}

bool ethernet_has_initialized = false;
void ethernet_setup() {
    if (ethernet_has_initialized) return;
    ethernet_has_initialized = true;
    
    oled_print("Starting up ... ", 0, 0);

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

    sprintf(mac_address, "%02x:%02x:%02x:%02x:%02x:%02x", local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);
    sprintf(msg, " %s", mac_address);
    IWatchdog.reload();
    oled_print(msg, 0, 7);
    
    // Initialize state machine
    ethState.state = ETH_STATE_INIT_START;
    ethState.stateEnteredAt = millis();
    
    // Run state machine until initial connection (with timeout)
    // Or set to non-blocking mode by removing this loop
    uint32_t startupTimeout = millis() + 10000; // 10 second startup timeout
    while (!ethState.isReady() && millis() < startupTimeout) {
        ethernet_state_machine_update();
        IWatchdog.reload();
        
        // Allow early exit if disconnected
        if (ethState.state == ETH_STATE_DISCONNECTED) break;
        if (ethState.state == ETH_STATE_ERROR && ethState.timeInState() > 1000) break;
    }
    
    oled_print("                ", 0, 0); // Clear the first line
    IWatchdog.reload();
    communication_idle.set(60000);
    
    Serial.printf("[ETH] Setup complete, state: %s, ready: %s\n", 
                  ethState.getStateName(), 
                  ethState.isReady() ? "YES" : "NO");
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

// Non-blocking message send with callback
typedef void (*SendMessageCallback)(bool success);

struct PendingMessage {
    bool active = false;
    IPAddress host;
    uint16_t port;
    char message[256];
    EthernetClient client;
    uint32_t startTime;
    SendMessageCallback callback = nullptr;
    
    enum State { IDLE, CONNECTING, SENDING, DONE } state = IDLE;
};

PendingMessage pendingMsg;

// Non-blocking send message - returns immediately, use callback for result
bool sendMessageAsync(IPAddress host, uint16_t port, const char* message, SendMessageCallback callback = nullptr) {
    if (pendingMsg.active) return false; // Already sending
    if (!ethState.isReady()) return false; // Network not ready
    
    pendingMsg.active = true;
    pendingMsg.host = host;
    pendingMsg.port = port;
    strncpy(pendingMsg.message, message, sizeof(pendingMsg.message) - 1);
    pendingMsg.message[sizeof(pendingMsg.message) - 1] = '\0';
    pendingMsg.callback = callback;
    pendingMsg.startTime = millis();
    pendingMsg.state = PendingMessage::CONNECTING;
    
    return true;
}

// Process pending async message (call from loop)
void processAsyncMessage() {
    if (!pendingMsg.active) return;
    
    uint32_t elapsed = millis() - pendingMsg.startTime;
    
    switch (pendingMsg.state) {
        case PendingMessage::CONNECTING: {
            spi_select(SPI_Ethernet);
            if (pendingMsg.client.connect(pendingMsg.host, pendingMsg.port)) {
                pendingMsg.state = PendingMessage::SENDING;
            } else if (elapsed > 5000) {
                // Connection timeout
                pendingMsg.active = false;
                pendingMsg.state = PendingMessage::IDLE;
                if (pendingMsg.callback) pendingMsg.callback(false);
            }
            break;
        }
        
        case PendingMessage::SENDING: {
            TCP_send(pendingMsg.client, pendingMsg.message);
            pendingMsg.client.flush();
            pendingMsg.client.stop();
            pendingMsg.active = false;
            pendingMsg.state = PendingMessage::IDLE;
            if (pendingMsg.callback) pendingMsg.callback(true);
            communication_idle.reset();
            break;
        }
        
        default:
            pendingMsg.active = false;
            pendingMsg.state = PendingMessage::IDLE;
            break;
    }
    
    spi_select(SPI_None);
}

// Blocking send message (legacy compatibility)
bool sendMessage(IPAddress host, uint16_t port, char* message) {
    if (!ethState.isReady()) {
        return false;
    }
    
    communication_idle.reset();
    spi_select(SPI_Ethernet);
    EthernetClient client;
    
    if (client.connect(host, port)) {
        TCP_send(client, message);
        client.flush();
        client.stop();
        IWatchdog.reload();
        spi_select(SPI_None);
        return true;
    } else {
        IWatchdog.reload();
        spi_select(SPI_None);
        return false;
    }
}

bool sendMessage(const char* host, uint16_t port, char* message) {
    if (!ethState.isReady()) {
        return false;
    }
    
    communication_idle.reset();
    spi_select(SPI_Ethernet);
    EthernetClient client;
    
    if (client.connect(host, port)) {
        TCP_send(client, message);
        client.flush();
        client.stop();
        IWatchdog.reload();
        spi_select(SPI_None);
        return true;
    } else {
        IWatchdog.reload();
        spi_select(SPI_None);
        return false;
    }
}

// ============================================================================
// Main Ethernet Loop (non-blocking)
// ============================================================================

TON ip_null_timeout(120000);
uint32_t ethernet_loop_time = 0;

void ethernet_loop() {
    uint32_t t = millis();
    int32_t dt = t - ethernet_loop_time;
    if (dt < 1) dt = 1;
    ethernet_loop_time = t;
    
    // Update the state machine (non-blocking)
    ethernet_state_machine_update();
    
    // Process any pending async messages
    processAsyncMessage();
    
    // Handle IP null timeout (safety check)
    bool ip_is_null = local_ip[0] == 0 && local_ip[1] == 0 && local_ip[2] == 0 && local_ip[3] == 0;
    bool ip_is_null_for_too_long = ip_null_timeout.update(ip_is_null && ethState.isReady(), dt);
    
    if (ip_is_null_for_too_long) {
        Serial.println("[ETH] IP null for too long, requesting hard reset");
        ip_null_timeout.update(false, dt);
        eth_request_hard_reset(true);
    }
    
    // Run web server only when network is ready
#ifdef USE_REST_API_SERVER
    if (ethState.isReady()) {
        web_server_loop();
    }
#endif // USE_REST_API_SERVER
}

// ============================================================================
// Utility Functions
// ============================================================================

// Check if ethernet is ready for communication
bool ethernet_ready() {
    return ethState.isReady();
}

// Check if ethernet is busy (connecting, resetting, etc.)
bool ethernet_busy() {
    return ethState.isBusy();
}

// Get current state name (for debugging)
const char* ethernet_state_name() {
    return ethState.getStateName();
}

// Get detailed status as JSON
void ethernet_status_json(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize,
        "{\"state\":\"%s\",\"ready\":%s,\"busy\":%s,\"link\":%s,\"server\":%s,\"ip\":\"%s\",\"mac\":\"%s\",\"initCycle\":%d,\"retries\":%d}",
        ethState.getStateName(),
        ethState.isReady() ? "true" : "false",
        ethState.isBusy() ? "true" : "false",
        ethState.linkEstablished ? "true" : "false",
        ethState.serverReady ? "true" : "false",
        ip_address,
        mac_address,
        ethState.initCycle,
        ethState.retryCount
    );
}