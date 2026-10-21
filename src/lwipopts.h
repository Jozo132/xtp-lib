#pragma once

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0
#define LWIP_RAW                        1
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ETHERNET_CRC_CHECK         1
#define LWIP_ICMP                       1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_DHCP                       1
#define LWIP_DNS                        1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_TIMERS                     1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (16 * 1024)
#define MEMP_NUM_PBUF                   16
#define PBUF_POOL_SIZE                  12
#define PBUF_POOL_BUFSIZE               1536
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                16
#define LWIP_PLATFORM_BYTESWAP          1
#define SYS_ARCH_DECL_PROTECT(level)    uint32_t level
#define SYS_ARCH_PROTECT(level)         do { (level) = 0; } while (0)
#define SYS_ARCH_UNPROTECT(level)       do { (void)(level); } while (0)
