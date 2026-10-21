#include "lan8740_lwip.h"

#include <Arduino.h>
#include <string.h>

#include "stm32h7xx_hal.h"

extern "C" {
#include <lwip/etharp.h>
#include <lwip/dhcp.h>
#include <lwip/init.h>
#include <lwip/mem.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>
}

namespace {

constexpr uint32_t PHY_ADDRESS = 0;
constexpr uint16_t PHY_REG_BMCR = 0;
constexpr uint16_t PHY_REG_BMSR = 1;
constexpr uint16_t PHY_BMCR_RESET = 0x8000;
constexpr uint16_t PHY_BMCR_AUTONEGOTIATION = 0x1000;
constexpr uint16_t PHY_BMCR_RESTART_AUTONEGOTIATION = 0x0200;
constexpr uint16_t PHY_BMSR_LINK_STATUS = 0x0004;
constexpr uint32_t RX_BUFFER_SIZE = 1536;
constexpr uint32_t LINK_POLL_MS = 250;
constexpr uint32_t DHCP_TIMEOUT_MS = XTP3_ETH_DHCP_TIMEOUT_MS;

ETH_HandleTypeDef ethHandle = {};
ETH_DMADescTypeDef rxDescriptors[ETH_RX_DESC_CNT]
    __attribute__((section(".eth_dma"), aligned(32)));
ETH_DMADescTypeDef txDescriptors[ETH_TX_DESC_CNT]
    __attribute__((section(".eth_dma"), aligned(32)));
uint8_t rxBuffers[ETH_RX_DESC_CNT][RX_BUFFER_SIZE]
    __attribute__((section(".eth_dma"), aligned(32)));
uint8_t txBuffer[RX_BUFFER_SIZE]
    __attribute__((section(".eth_dma"), aligned(32)));

struct Context {
  xtp_lan8740::Config config = {};
  ip4_addr_t activeAddress = {};
  netif interface = {};
  tcp_pcb* echoListener = nullptr;
  uint8_t rxBufferIndex = 0;
  uint32_t lastLinkPoll = 0;
  uint32_t rxFrames = 0;
  uint32_t txFrames = 0;
  uint32_t dropped = 0;
  bool initialized = false;
  bool link = false;
  bool dhcpEnabled = false;
  bool dhcpBound = false;
  bool dhcpFallback = false;
  bool serverStarted = false;
  uint32_t dhcpStartedAt = 0;
};

Context context;

void configureDmaMemory() {
  MPU_Region_InitTypeDef region = {};
  region.Enable = MPU_REGION_ENABLE;
  region.Number = MPU_REGION_NUMBER7;
  region.BaseAddress = 0x30000000;
  region.Size = MPU_REGION_SIZE_32KB;
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.TypeExtField = MPU_TEX_LEVEL0;
  region.SubRegionDisable = 0x00;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;

  HAL_MPU_Disable();
  HAL_MPU_ConfigRegion(&region);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void configureGpio() {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_ETH1MAC_CLK_ENABLE();
  __HAL_RCC_ETH1TX_CLK_ENABLE();
  __HAL_RCC_ETH1RX_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF11_ETH;

  gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOC, &gpio);

  GPIO_InitTypeDef reset = {};
  reset.Pin = GPIO_PIN_14;
  reset.Mode = GPIO_MODE_OUTPUT_PP;
  reset.Pull = GPIO_NOPULL;
  reset.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &reset);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_SET);

  GPIO_InitTypeDef interrupt = {};
  interrupt.Pin = GPIO_PIN_15;
  interrupt.Mode = GPIO_MODE_INPUT;
  interrupt.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &interrupt);
}

void resetPhy() {
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_RESET);
  delay(2);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_SET);
  delay(50);
}

bool configurePhy() {
  if (HAL_ETH_WritePHYRegister(&ethHandle, PHY_ADDRESS, PHY_REG_BMCR,
                               PHY_BMCR_RESET) != HAL_OK) {
    return false;
  }

  const uint32_t started = millis();
  uint32_t value = 0;
  do {
    if (HAL_ETH_ReadPHYRegister(&ethHandle, PHY_ADDRESS, PHY_REG_BMCR,
                                &value) != HAL_OK) {
      return false;
    }
  } while ((value & PHY_BMCR_RESET) != 0 && millis() - started < 500);

  if ((value & PHY_BMCR_RESET) != 0) {
    return false;
  }

  return HAL_ETH_WritePHYRegister(
             &ethHandle, PHY_ADDRESS, PHY_REG_BMCR,
             PHY_BMCR_AUTONEGOTIATION | PHY_BMCR_RESTART_AUTONEGOTIATION) ==
         HAL_OK;
}

bool readPhyLink() {
  uint32_t status = 0;
  if (HAL_ETH_ReadPHYRegister(&ethHandle, PHY_ADDRESS, PHY_REG_BMSR,
                              &status) != HAL_OK) {
    return false;
  }
  // BMSR link status is latch-low; the second read reports the current state.
  if (HAL_ETH_ReadPHYRegister(&ethHandle, PHY_ADDRESS, PHY_REG_BMSR,
                              &status) != HAL_OK) {
    return false;
  }
  return (status & PHY_BMSR_LINK_STATUS) != 0;
}

err_t lowLevelOutput(netif* interface, pbuf* packet) {
  if (packet == nullptr || packet->tot_len > sizeof(txBuffer)) {
    return ERR_BUF;
  }

  pbuf_copy_partial(packet, txBuffer, packet->tot_len, 0);
  SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(txBuffer),
                          sizeof(txBuffer));

  ETH_BufferTypeDef buffer = {};
  buffer.buffer = txBuffer;
  buffer.len = packet->tot_len;

  ETH_TxPacketConfigTypeDef transmit = {};
  transmit.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
  transmit.Length = packet->tot_len;
  transmit.TxBuffer = &buffer;
  transmit.SrcAddrCtrl = ETH_SRC_ADDR_INSERT;
  transmit.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  transmit.ChecksumCtrl = ETH_CHECKSUM_DISABLE;

  if (HAL_ETH_Transmit(&ethHandle, &transmit, 100) != HAL_OK) {
    return ERR_IF;
  }
  ++context.txFrames;
  HAL_ETH_ReleaseTxPacket(&ethHandle);
  (void)interface;
  return ERR_OK;
}

err_t lowLevelInit(netif* interface) {
  interface->name[0] = 'X';
  interface->name[1] = 'L';
  interface->output = etharp_output;
  interface->linkoutput = lowLevelOutput;
  netif_set_hostname(interface, context.config.hostname);
  interface->hwaddr_len = 6;
  memcpy(interface->hwaddr, context.config.mac, 6);
  interface->mtu = 1500;
  interface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  return ERR_OK;
}

void updateLink() {
  const bool link = readPhyLink();
  if (link == context.link) {
    return;
  }

  context.link = link;
  if (link) {
    netif_set_link_up(&context.interface);
  } else {
    netif_set_link_down(&context.interface);
  }
}

err_t echoReceive(void* argument, tcp_pcb* pcb, pbuf* packet, err_t error) {
  (void)argument;
  if (packet == nullptr) {
    tcp_close(pcb);
    return ERR_OK;
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    return error;
  }

  tcp_recved(pcb, packet->tot_len);
  for (pbuf* part = packet; part != nullptr; part = part->next) {
    if (tcp_write(pcb, part->payload, part->len, TCP_WRITE_FLAG_COPY) !=
        ERR_OK) {
      pbuf_free(packet);
      return ERR_MEM;
    }
  }
  tcp_output(pcb);
  pbuf_free(packet);
  return ERR_OK;
}

void echoError(void* argument, err_t error) {
  (void)argument;
  (void)error;
}

err_t echoAccept(void* argument, tcp_pcb* pcb, err_t error) {
  (void)argument;
  if (error != ERR_OK) {
    return error;
  }
  tcp_err(pcb, echoError);
  tcp_recv(pcb, echoReceive);
  return ERR_OK;
}

bool startEchoServer() {
  tcp_pcb* listener = tcp_new();
  if (listener == nullptr) {
    return false;
  }
  if (tcp_bind(listener, IP4_ADDR_ANY, context.config.echoPort) != ERR_OK) {
    tcp_abort(listener);
    return false;
  }
  context.echoListener = tcp_listen(listener);
  if (context.echoListener == nullptr) {
    return false;
  }
  tcp_accept(context.echoListener, echoAccept);
  return true;
}

bool startServer() {
  if (context.serverStarted) {
    return true;
  }
  context.serverStarted = startEchoServer();
  return context.serverStarted;
}

void applyStaticAddress() {
  netif_set_addr(&context.interface, &context.config.address,
                 &context.config.netmask, &context.config.gateway);
  context.activeAddress = context.config.address;
}

bool applyDhcpLease() {
  if (!dhcp_supplied_address(&context.interface)) {
    return false;
  }

  context.activeAddress = *netif_ip4_addr(&context.interface);
  context.dhcpBound = true;
  context.dhcpFallback = false;
  return startServer();
}

void finishDhcp() {
  if (applyDhcpLease()) {
    return;
  }

  if (millis() - context.dhcpStartedAt < DHCP_TIMEOUT_MS) {
    return;
  }

  dhcp_release_and_stop(&context.interface);
  applyStaticAddress();
  context.dhcpEnabled = false;
  context.dhcpBound = false;
  context.dhcpFallback = true;
  startServer();
}

}  // namespace

extern "C" uint32_t xtp_lwip_rand(void) {
  return HAL_GetTick();
}

extern "C" void HAL_ETH_MspInit(ETH_HandleTypeDef* handle) {
  if (handle->Instance == ETH) {
    configureGpio();
  }
}

extern "C" void HAL_ETH_RxAllocateCallback(uint8_t** buffer) {
  *buffer = rxBuffers[context.rxBufferIndex++ % ETH_RX_DESC_CNT];
}

extern "C" void HAL_ETH_RxLinkCallback(void** start, void** end,
                                        uint8_t* buffer, uint16_t length) {
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(buffer),
                                RX_BUFFER_SIZE);
  pbuf* segment = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
  if (segment == nullptr || pbuf_take(segment, buffer, length) != ERR_OK) {
    if (segment != nullptr) {
      pbuf_free(segment);
    }
    ++context.dropped;
    return;
  }

  pbuf* tail = segment;
  while (tail->next != nullptr) {
    tail = tail->next;
  }
  if (*start == nullptr) {
    *start = segment;
  } else {
    static_cast<pbuf*>(*end)->next = segment;
  }
  *end = tail;
}

extern "C" u32_t sys_now(void) {
  return HAL_GetTick();
}

namespace xtp_lan8740 {

bool begin(const Config& config) {
  context = {};
  context.config = config;
  context.activeAddress = config.address;
  configureDmaMemory();
  resetPhy();

  ethHandle.Instance = ETH;
  ethHandle.Init.MACAddr = context.config.mac;
  ethHandle.Init.MediaInterface = HAL_ETH_RMII_MODE;
  ethHandle.Init.TxDesc = txDescriptors;
  ethHandle.Init.RxDesc = rxDescriptors;
  ethHandle.Init.RxBuffLen = RX_BUFFER_SIZE;

  if (HAL_ETH_Init(&ethHandle) != HAL_OK || !configurePhy()) {
    return false;
  }

  ETH_MACConfigTypeDef mac = {};
  if (HAL_ETH_GetMACConfig(&ethHandle, &mac) != HAL_OK) {
    return false;
  }
  mac.Speed = ETH_SPEED_100M;
  mac.DuplexMode = ETH_FULLDUPLEX_MODE;
  if (HAL_ETH_SetMACConfig(&ethHandle, &mac) != HAL_OK ||
      HAL_ETH_Start(&ethHandle) != HAL_OK) {
    return false;
  }

  lwip_init();
  ip4_addr_t zeroAddress = {};
  ip4_addr_t* address = config.dhcp ? &zeroAddress : &context.config.address;
  ip4_addr_t* netmask = config.dhcp ? &zeroAddress : &context.config.netmask;
  ip4_addr_t* gateway = config.dhcp ? &zeroAddress : &context.config.gateway;
  if (netif_add(&context.interface, address, netmask, gateway, nullptr,
                lowLevelInit, ethernet_input) == nullptr) {
    return false;
  }
  netif_set_default(&context.interface);
  netif_set_up(&context.interface);
  updateLink();

  if (config.dhcp) {
    context.dhcpEnabled = true;
    context.dhcpStartedAt = millis();
    if (dhcp_start(&context.interface) != ERR_OK) {
      return false;
    }
  } else {
    if (!startServer()) {
      return false;
    }
  }

  context.initialized = true;
  return true;
}

void poll() {
  if (!context.initialized) {
    return;
  }

  sys_check_timeouts();
  void* packet = nullptr;
  while (HAL_ETH_ReadData(&ethHandle, &packet) == HAL_OK) {
    ++context.rxFrames;
    if (packet == nullptr || context.interface.input(
                                static_cast<pbuf*>(packet),
                                &context.interface) != ERR_OK) {
      if (packet != nullptr) {
        pbuf_free(static_cast<pbuf*>(packet));
      }
    }
    packet = nullptr;
  }

  if (context.dhcpEnabled && !context.dhcpBound) {
    finishDhcp();
  }

  const uint32_t now = millis();
  if (now - context.lastLinkPoll >= LINK_POLL_MS) {
    context.lastLinkPoll = now;
    updateLink();
  }
}

bool ready() {
  return context.initialized && context.link && context.serverStarted;
}

bool linkUp() {
  return context.link;
}

bool serverReady() {
  return context.serverStarted;
}

bool dhcpInProgress() {
  return context.dhcpEnabled && !context.dhcpBound;
}

bool dhcpFallbackActive() {
  return context.dhcpFallback;
}

const ip4_addr_t& address() {
  return context.activeAddress;
}

uint32_t rxFrames() {
  return context.rxFrames;
}

uint32_t txFrames() {
  return context.txFrames;
}

uint32_t rxDropped() {
  return context.dropped;
}

}  // namespace xtp_lan8740
