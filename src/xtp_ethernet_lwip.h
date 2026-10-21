#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "lan8740_lwip.h"
#include "xtp_lwip_compat.h"

#ifndef XTP3_ETH_IP
#define XTP3_ETH_IP "192.168.1.50"
#endif

#ifndef XTP3_ETH_NETMASK
#define XTP3_ETH_NETMASK "255.255.255.0"
#endif

#ifndef XTP3_ETH_GATEWAY
#define XTP3_ETH_GATEWAY "192.168.1.1"
#endif

#ifndef XTP3_ETH_ECHO_PORT
#define XTP3_ETH_ECHO_PORT 7
#endif

#ifndef XTP3_ETH_DHCP
#define XTP3_ETH_DHCP 1
#endif

#ifndef XTP3_ETH_HOSTNAME
#define XTP3_ETH_HOSTNAME "xtp3"
#endif

extern uint8_t DEVICE_UID[4];
extern uint8_t local_mac[6];
extern uint8_t local_ip[4];

enum EthernetState {
  ETH_STATE_IDLE,
  ETH_STATE_HARD_RESET_START,
  ETH_STATE_HARD_RESET_LOW,
  ETH_STATE_HARD_RESET_WAIT,
  ETH_STATE_SOFT_RESET_START,
  ETH_STATE_SOFT_RESET_WAIT,
  ETH_STATE_INIT_START,
  ETH_STATE_INIT_CHECK_LINK,
  ETH_STATE_INIT_DHCP,
  ETH_STATE_INIT_STATIC,
  ETH_STATE_INIT_SERVER_START,
  ETH_STATE_INIT_COMPLETE,
  ETH_STATE_DHCP_RETRY,
  ETH_STATE_DISCONNECTED,
  ETH_STATE_ERROR,
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
  bool dhcpFallbackActive = false;
  uint32_t lastDhcpRetry = 0;
  uint8_t retryCount = 0;
  uint8_t initCycle = 0;

  static constexpr uint32_t MIN_HARD_RESET_INTERVAL = 90000;
  static constexpr uint32_t MIN_SOFT_RESET_INTERVAL = 45000;

  void enterState(EthernetState newState) {
    if (state != newState) {
      previousState = state;
      state = newState;
      stateEnteredAt = millis();
    }
  }

  uint32_t timeInState() const { return millis() - stateEnteredAt; }

  bool isReady() const {
    return state == ETH_STATE_IDLE && linkEstablished && serverReady;
  }

  bool isBusy() const {
    return state != ETH_STATE_IDLE && state != ETH_STATE_DISCONNECTED &&
           state != ETH_STATE_ERROR;
  }

  const char* getStateName() const {
    switch (state) {
      case ETH_STATE_IDLE: return "IDLE";
      case ETH_STATE_DISCONNECTED: return "DISCONNECTED";
      case ETH_STATE_ERROR: return "ERROR";
      default: return "INITIALIZING";
    }
  }
};

extern EthernetStateMachine ethState;
extern char ip_address[20];
extern char mac_address[18];
extern uint32_t ethernet_cycle;
extern bool ethernet_link_established;
extern int ethernet_link_status;
extern bool ethernet_has_initialized;

void eth_request_hard_reset(bool force = false);
void eth_request_soft_reset(bool force = false);
void eth_request_reconnect();
void ethernet_init();
void ethernet_setup();
void ethernet_loop();
bool ethernet_is_connected();
bool ethernet_ready();
bool ethernet_busy();
const char* ethernet_state_name();
void ethernet_status_json(char* buffer, size_t bufferSize);
void w5500_soft_reset(bool force = false);
void w5500_hard_reset(bool force = false);

using XtpTcpClient = xtp_lwip::TcpClient;
using XtpWsClient = xtp_lwip::WebSocketClient;
using RestServer = xtp_lwip::HttpServer;

void web_server_setup();
void web_server_loop();
