#pragma once

#include <stdint.h>

extern "C" {
#include <lwip/ip4_addr.h>
}

#ifndef XTP3_ETH_DHCP_TIMEOUT_MS
#define XTP3_ETH_DHCP_TIMEOUT_MS 60000UL
#endif

namespace xtp_lan8740 {

struct Config {
  uint8_t mac[6];
  ip4_addr_t address;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
  uint16_t echoPort = 7;
  bool dhcp = false;
  const char* hostname = nullptr;
};

bool begin(const Config& config);
void poll();
bool ready();
bool linkUp();
bool serverReady();
bool dhcpInProgress();
bool dhcpFallbackActive();
const ip4_addr_t& address();
uint32_t rxFrames();
uint32_t txFrames();
uint32_t rxDropped();

}  // namespace xtp_lan8740
