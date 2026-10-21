#include "xtp_ethernet_lwip.h"

#include <stdio.h>
#include <string.h>

void oled_print(const char* message, int x, int y);
void oled_set_reconnect_callback(void (*callback)());

EthernetStateMachine ethState;
char ip_address[20] = "0.0.0.0";
char mac_address[18] = "";
uint32_t ethernet_cycle = 0;
bool ethernet_link_established = false;
int ethernet_link_status = 0;
bool ethernet_has_initialized = false;

namespace {

xtp_lan8740::Config networkConfig = {};

void refreshNetworkDisplay() {
  char ipLine[20];
  if (ethState.linkEstablished && ethState.serverReady) {
    snprintf(ipLine, sizeof(ipLine), "  %s", ip_address);
  } else if (ethState.state == ETH_STATE_DISCONNECTED) {
    snprintf(ipLine, sizeof(ipLine), "   Disconnected");
  } else if (ethState.isBusy()) {
    snprintf(ipLine, sizeof(ipLine), "  Connecting...");
  } else {
    snprintf(ipLine, sizeof(ipLine), "      ??????   ");
  }

  size_t length = strlen(ipLine);
  while (length < 19) {
    ipLine[length++] = ' ';
  }
  ipLine[length] = '\0';

  oled_print(mac_address, 1, 7);
  oled_print(ipLine, 1, 6);
}

void copyAddressToLegacyGlobals() {
  const ip4_addr_t& address = xtp_lan8740::address();
  const uint32_t rawAddress = address.addr;
  local_ip[0] = static_cast<uint8_t>(rawAddress & 0xffU);
  local_ip[1] = static_cast<uint8_t>((rawAddress >> 8) & 0xffU);
  local_ip[2] = static_cast<uint8_t>((rawAddress >> 16) & 0xffU);
  local_ip[3] = static_cast<uint8_t>((rawAddress >> 24) & 0xffU);
  snprintf(ip_address, sizeof(ip_address), "%u.%u.%u.%u", local_ip[0],
           local_ip[1], local_ip[2], local_ip[3]);
  snprintf(mac_address, sizeof(mac_address), "%02x:%02x:%02x:%02x:%02x:%02x",
           local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4],
           local_mac[5]);
  refreshNetworkDisplay();
}

void configureNetwork() {
  memcpy(networkConfig.mac, local_mac, sizeof(networkConfig.mac));
  const bool validAddress = ip4addr_aton(XTP3_ETH_IP, &networkConfig.address);
  const bool validNetmask =
      ip4addr_aton(XTP3_ETH_NETMASK, &networkConfig.netmask);
  const bool validGateway =
      ip4addr_aton(XTP3_ETH_GATEWAY, &networkConfig.gateway);
  if (!validAddress || networkConfig.address.addr == 0 || !validNetmask ||
      !validGateway) {
    networkConfig.address.addr = 0x3201A8C0UL;
    networkConfig.netmask.addr = 0x00FFFFFFUL;
    networkConfig.gateway.addr = 0x0101A8C0UL;
  }
  networkConfig.echoPort = XTP3_ETH_ECHO_PORT;
  networkConfig.dhcp = XTP3_ETH_DHCP != 0;
  networkConfig.hostname = XTP3_ETH_HOSTNAME;
}

bool startNetwork() {
  configureNetwork();
  const bool started = xtp_lan8740::begin(networkConfig);
  ethernet_link_established = started && xtp_lan8740::linkUp();
  ethernet_link_status = ethernet_link_established ? 1 : 0;
  ethState.linkEstablished = ethernet_link_established;
  ethState.serverReady = started && xtp_lan8740::serverReady();
  ethState.initialized = started;
  ethState.state = !started
                       ? ETH_STATE_ERROR
                       : ethState.serverReady ? ETH_STATE_IDLE
                                              : ETH_STATE_INIT_DHCP;
  ethState.stateEnteredAt = millis();
  if (started) {
    ++ethState.initCycle;
    ++ethernet_cycle;
    copyAddressToLegacyGlobals();
  }
  return started;
}

}  // namespace

void eth_request_hard_reset(bool force) {
  if (!force && millis() - ethState.lastHardReset <
                    EthernetStateMachine::MIN_HARD_RESET_INTERVAL) {
    return;
  }
  ethState.lastHardReset = millis();
  startNetwork();
}

void eth_request_soft_reset(bool force) {
  if (!force && millis() - ethState.lastSoftReset <
                    EthernetStateMachine::MIN_SOFT_RESET_INTERVAL) {
    return;
  }
  ethState.lastSoftReset = millis();
  startNetwork();
}

void eth_request_reconnect() { startNetwork(); }

void ethernet_init() { eth_request_reconnect(); }

void ethernet_setup() {
  if (ethernet_has_initialized) return;
  ethernet_has_initialized = true;

  oled_set_reconnect_callback(refreshNetworkDisplay);

  local_mac[0] = 0x02;
  local_mac[1] = 0x58;
  local_mac[2] = 0x54;
  local_mac[3] = 0x50;
  local_mac[4] = DEVICE_UID[0];
  local_mac[5] = DEVICE_UID[1];
  startNetwork();
}

bool ethernet_is_connected() {
  ethernet_link_established = xtp_lan8740::linkUp();
  ethernet_link_status = ethernet_link_established ? 1 : 0;
  ethState.linkEstablished = ethernet_link_established;
  if (!ethernet_link_established) {
    local_ip[0] = 0;
    local_ip[1] = 0;
    local_ip[2] = 0;
    local_ip[3] = 0;
    strcpy(ip_address, "0.0.0.0");
  }
  return ethernet_link_established;
}

void ethernet_loop() {
  xtp_lan8740::poll();
  xtp_lwip::poll();
  const bool previousLink = ethState.linkEstablished;
  const EthernetState previousState = ethState.state;
  const bool previousServer = ethState.serverReady;
  const bool connected = ethernet_is_connected();
  ethState.serverReady = xtp_lan8740::serverReady();
  ethState.state = !connected
                       ? ETH_STATE_DISCONNECTED
                       : ethState.serverReady ? ETH_STATE_IDLE
                                              : ETH_STATE_INIT_DHCP;
  if (connected && ethState.serverReady &&
      (!previousLink || !previousServer ||
       strcmp(ip_address, "0.0.0.0") == 0)) {
    copyAddressToLegacyGlobals();
  }
  if (previousLink != connected || previousServer != ethState.serverReady ||
      previousState != ethState.state) {
    refreshNetworkDisplay();
  }
}

bool ethernet_ready() { return ethState.isReady(); }

bool ethernet_busy() { return ethState.isBusy(); }

const char* ethernet_state_name() { return ethState.getStateName(); }

void ethernet_status_json(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;
  snprintf(buffer, bufferSize,
           "{\"state\":\"%s\",\"ready\":%s,\"busy\":%s,"
           "\"link\":%s,\"server\":%s,\"ip\":\"%s\","
           "\"mac\":\"%s\",\"initCycle\":%u,\"retries\":%u}",
           ethState.getStateName(), ethState.isReady() ? "true" : "false",
           ethState.isBusy() ? "true" : "false",
           ethState.linkEstablished ? "true" : "false",
           ethState.serverReady ? "true" : "false", ip_address, mac_address,
           ethState.initCycle, ethState.retryCount);
}

void w5500_soft_reset(bool force) { eth_request_soft_reset(force); }

void w5500_hard_reset(bool force) { eth_request_hard_reset(force); }

void web_server_setup() {}

void web_server_loop() {}
