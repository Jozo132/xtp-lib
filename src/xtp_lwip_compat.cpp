#include "xtp_lwip_compat.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

extern "C" {
#include <lwip/err.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
}

namespace xtp_lwip {
namespace {

constexpr uint16_t UDP_BUFFER_SIZE = 1536;
constexpr uint16_t TCP_BUFFER_SIZE = 2048;
constexpr uint8_t HTTP_MAX_ROUTES = 32;
constexpr uint16_t HTTP_REQUEST_SIZE = 2048;
constexpr uint16_t HTTP_RESPONSE_SIZE = 2048;
constexpr uint8_t SNTP_CLIENT_MAX = 4;

struct UdpState {
  udp_pcb* pcb = nullptr;
  ip4_addr_t destination = {};
  uint16_t destinationPort = 0;
  uint8_t txBuffer[UDP_BUFFER_SIZE] = {};
  uint16_t txLength = 0;
  uint8_t rxBuffer[UDP_BUFFER_SIZE] = {};
  uint16_t rxLength = 0;
  uint16_t rxOffset = 0;
  ip4_addr_t remote = {};
  uint16_t remotePort = 0;
  UdpSocket::PacketCallback callback = nullptr;
};

struct TcpState {
  tcp_pcb* pcb = nullptr;
  TcpClient::State state = TcpClient::IDLE;
  bool closePending = false;
  uint8_t rxBuffer[TCP_BUFFER_SIZE] = {};
  uint16_t rxHead = 0;
  uint16_t rxTail = 0;
  ConnectCallback onConnect = nullptr;
  DisconnectCallback onDisconnect = nullptr;
  DataCallback onData = nullptr;
};

struct TcpServerState {
  tcp_pcb* listener = nullptr;
  TcpClient client;
};

struct HttpRoute {
  const char* uri = nullptr;
  HttpMethod method = HTTP_GET;
  HttpServer::EndpointHandler handler = nullptr;
};

struct HttpState {
  tcp_pcb* listener = nullptr;
  tcp_pcb* client = nullptr;
  HttpRoute routes[HTTP_MAX_ROUTES] = {};
  uint8_t routeCount = 0;
  HttpServer::EndpointHandler notFound = nullptr;
  HttpMethod method = HTTP_GET;
  char uri[64] = {};
  char body[HTTP_REQUEST_SIZE + 1] = {};
  char request[HTTP_REQUEST_SIZE + 1] = {};
  uint16_t requestLength = 0;
  char response[HTTP_RESPONSE_SIZE] = {};
  uint16_t responseLength = 0;
  int responseCode = 200;
  const char* responseType = "text/plain";
};

struct SntpState {
  UdpSocket socket;
  bool waiting = false;
  bool synchronized = false;
  uint32_t unixSeconds = 0;
  TimeCallback callback = nullptr;
};

struct WebSocketState {
  tcp_pcb* pcb = nullptr;
  WebSocketClient::State state = WebSocketClient::DISCONNECTED;
  ip4_addr_t address = {};
  uint16_t port = 80;
  char host[64] = {};
  char path[64] = "/";
  uint8_t rxBuffer[HTTP_REQUEST_SIZE] = {};
  uint16_t rxLength = 0;
  ConnectCallback onConnect = nullptr;
  DisconnectCallback onDisconnect = nullptr;
  WebSocketMessageCallback onMessage = nullptr;
};

struct WebSocketServerState {
  tcp_pcb* listener = nullptr;
  tcp_pcb* client = nullptr;
  bool handshakeComplete = false;
  uint8_t rxBuffer[HTTP_REQUEST_SIZE] = {};
  uint16_t rxLength = 0;
  WebSocketServer::MessageCallback callback = nullptr;
};

SntpClient* sntpClients[SNTP_CLIENT_MAX] = {};

UdpState* udpState(UdpSocket* socket) {
  return reinterpret_cast<UdpState*>(socket->nativeState());
}

TcpState* tcpState(TcpClient* client) {
  return reinterpret_cast<TcpState*>(client->nativeState());
}

TcpServerState* tcpServerState(TcpServer* server) {
  return reinterpret_cast<TcpServerState*>(server->nativeState());
}

HttpState* httpState(HttpServer* server) {
  return reinterpret_cast<HttpState*>(server->nativeState());
}

SntpState* sntpState(SntpClient* client) {
  return reinterpret_cast<SntpState*>(client->nativeState());
}

WebSocketState* webSocketState(WebSocketClient* client) {
  return reinterpret_cast<WebSocketState*>(client->nativeState());
}

WebSocketServerState* webSocketServerState(WebSocketServer* server) {
  return reinterpret_cast<WebSocketServerState*>(server->nativeState());
}

void registerSntpClient(SntpClient* client) {
  for (uint8_t index = 0; index < SNTP_CLIENT_MAX; ++index) {
    if (sntpClients[index] == client) return;
    if (sntpClients[index] == nullptr) {
      sntpClients[index] = client;
      return;
    }
  }
}

void unregisterSntpClient(SntpClient* client) {
  for (uint8_t index = 0; index < SNTP_CLIENT_MAX; ++index) {
    if (sntpClients[index] == client) sntpClients[index] = nullptr;
  }
}

void udpReceive(void* argument, udp_pcb* pcb, pbuf* packet,
               const ip4_addr_t* address, uint16_t port) {
  auto* state = static_cast<UdpState*>(argument);
  if (state == nullptr || packet == nullptr) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    return;
  }

  const uint16_t length = packet->tot_len > UDP_BUFFER_SIZE
                              ? UDP_BUFFER_SIZE
                              : packet->tot_len;
  pbuf_copy_partial(packet, state->rxBuffer, length, 0);
  state->rxLength = length;
  state->rxOffset = 0;
  state->remote = *address;
  state->remotePort = port;

  if (state->callback != nullptr) {
    state->callback(state->remote, state->remotePort, state->rxBuffer,
                    state->rxLength);
  }
  pbuf_free(packet);
  (void)pcb;
}

err_t tcpConnected(void* argument, tcp_pcb* pcb, err_t error) {
  auto* state = static_cast<TcpState*>(argument);
  if (state == nullptr || error != ERR_OK) {
    return error;
  }
  state->pcb = pcb;
  state->state = TcpClient::CONNECTED;
  if (state->onConnect != nullptr) {
    state->onConnect();
  }
  return ERR_OK;
}

err_t tcpReceive(void* argument, tcp_pcb* pcb, pbuf* packet, err_t error) {
  auto* state = static_cast<TcpState*>(argument);
  if (state == nullptr) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    return ERR_ARG;
  }
  if (packet == nullptr) {
    state->state = TcpClient::IDLE;
    state->closePending = true;
    if (state->onDisconnect != nullptr) {
      state->onDisconnect("remote closed");
    }
    return ERR_OK;
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    return error;
  }

  uint16_t freeSpace = TCP_BUFFER_SIZE - 1 -
                       ((state->rxHead + TCP_BUFFER_SIZE - state->rxTail) %
                        TCP_BUFFER_SIZE);
  uint16_t copied = packet->tot_len < freeSpace ? packet->tot_len : freeSpace;
  for (uint16_t index = 0; index < copied; ++index) {
    uint8_t value = 0;
    pbuf_copy_partial(packet, &value, 1, index);
    state->rxBuffer[state->rxHead] = value;
    state->rxHead = (state->rxHead + 1) % TCP_BUFFER_SIZE;
  }
  tcp_recved(pcb, packet->tot_len);
  if (state->onData != nullptr && copied > 0) {
    state->onData(state->rxBuffer, copied);
  }
  pbuf_free(packet);
  return ERR_OK;
}

void tcpError(void* argument, err_t error) {
  auto* state = static_cast<TcpState*>(argument);
  if (state == nullptr) {
    return;
  }
  state->pcb = nullptr;
  state->state = TcpClient::IDLE;
  if (state->onDisconnect != nullptr) {
    (void)error;
    state->onDisconnect("tcp error");
  }
}

err_t tcpServerAccept(void* argument, tcp_pcb* pcb, err_t error) {
  auto* state = static_cast<TcpServerState*>(argument);
  if (state == nullptr || error != ERR_OK) {
    if (pcb != nullptr) tcp_abort(pcb);
    return error;
  }
  if (state->client.connected() || state->client.available() > 0) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  return state->client.adoptAccepted(pcb) ? ERR_OK : ERR_ABRT;
}

const char* httpReason(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "Response";
  }
}

err_t httpReceive(void* argument, tcp_pcb* pcb, pbuf* packet, err_t error);

err_t httpAccept(void* argument, tcp_pcb* pcb, err_t error) {
  auto* server = static_cast<HttpServer*>(argument);
  if (server == nullptr || error != ERR_OK) {
    return error;
  }
  auto* state = httpState(server);
  if (state->client != nullptr) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  state->client = pcb;
  tcp_arg(pcb, server);
  tcp_recv(pcb, httpReceive);
  tcp_err(pcb, tcpError);
  return ERR_OK;
}

void httpDispatch(HttpServer* server) {
  auto* state = httpState(server);
  char method[8] = {};
  char path[64] = {};
  if (sscanf(state->request, "%7s %63s", method, path) != 2) {
    server->send(400, "text/plain", "bad request");
    return;
  }

  state->method = strcmp(method, "POST") == 0 ? HTTP_POST : HTTP_GET;
  strncpy(state->uri, path, sizeof(state->uri) - 1);
  state->uri[sizeof(state->uri) - 1] = '\0';
  state->body[0] = '\0';
  char* bodyStart = strstr(state->request, "\r\n\r\n");
  if (bodyStart != nullptr) {
    bodyStart += 4;
    strncpy(state->body, bodyStart, sizeof(state->body) - 1);
    state->body[sizeof(state->body) - 1] = '\0';
  }

  for (uint8_t index = 0; index < state->routeCount; ++index) {
    if (state->routes[index].method == state->method &&
        strcmp(state->routes[index].uri, state->uri) == 0) {
      if (state->routes[index].handler != nullptr) {
        state->routes[index].handler();
      }
      return;
    }
  }
  if (state->notFound != nullptr) {
    state->notFound();
  } else {
    server->send(404, "text/plain", "not found");
  }
}

err_t httpReceive(void* argument, tcp_pcb* pcb, pbuf* packet, err_t error) {
  auto* server = static_cast<HttpServer*>(argument);
  if (server == nullptr) {
    if (packet != nullptr) pbuf_free(packet);
    return ERR_ARG;
  }
  auto* state = httpState(server);
  if (packet == nullptr) {
    state->client = nullptr;
    return tcp_close(pcb);
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    return error;
  }
  const uint16_t room = HTTP_REQUEST_SIZE - state->requestLength;
  const uint16_t copyLength = packet->tot_len < room ? packet->tot_len : room;
  pbuf_copy_partial(packet, state->request + state->requestLength, copyLength,
                    0);
  state->requestLength += copyLength;
  state->request[state->requestLength] = '\0';
  tcp_recved(pcb, packet->tot_len);
  pbuf_free(packet);
  if (strstr(state->request, "\r\n\r\n") != nullptr) {
    httpDispatch(server);
    state->requestLength = 0;
    state->request[0] = '\0';
  }
  return ERR_OK;
}

void webSocketDisconnect(WebSocketState* state, const char* reason) {
  if (state->pcb != nullptr) {
    tcp_arg(state->pcb, nullptr);
    if (tcp_close(state->pcb) != ERR_OK) {
      tcp_abort(state->pcb);
    }
  }
  state->pcb = nullptr;
  state->state = WebSocketClient::DISCONNECTED;
  if (state->onDisconnect != nullptr) {
    state->onDisconnect(reason);
  }
}

bool webSocketSendFrame(WebSocketState* state, uint8_t opcode,
                        const uint8_t* payload, uint16_t length) {
  if (state->pcb == nullptr || state->state != WebSocketClient::CONNECTED ||
      length > 2048) {
    return false;
  }
  uint8_t frame[2060] = {};
  uint16_t headerLength = 2;
  frame[0] = 0x80 | opcode;
  if (length < 126) {
    frame[1] = 0x80 | length;
  } else {
    frame[1] = 0x80 | 126;
    frame[2] = static_cast<uint8_t>(length >> 8);
    frame[3] = static_cast<uint8_t>(length);
    headerLength = 4;
  }
  const uint8_t mask[4] = {0x37, 0xC1, 0x4A, 0x8B};
  memcpy(frame + headerLength, mask, sizeof(mask));
  headerLength += sizeof(mask);
  for (uint16_t index = 0; index < length; ++index) {
    frame[headerLength + index] = payload[index] ^ mask[index % 4];
  }
  const uint16_t frameLength = headerLength + length;
  if (tcp_write(state->pcb, frame, frameLength, TCP_WRITE_FLAG_COPY) !=
      ERR_OK) {
    return false;
  }
  return tcp_output(state->pcb) == ERR_OK;
}

err_t webSocketConnected(void* argument, tcp_pcb* pcb, err_t error) {
  auto* state = static_cast<WebSocketState*>(argument);
  if (state == nullptr || error != ERR_OK) return error;
  state->pcb = pcb;
  state->state = WebSocketClient::HANDSHAKE;
  char request[256] = {};
  const int length = snprintf(
      request, sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\n"
      "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
      state->path, state->host, state->port);
  if (length <= 0 || tcp_write(pcb, request, length, TCP_WRITE_FLAG_COPY) !=
                         ERR_OK || tcp_output(pcb) != ERR_OK) {
    webSocketDisconnect(state, "handshake send failed");
  }
  return ERR_OK;
}

err_t webSocketReceive(void* argument, tcp_pcb* pcb, pbuf* packet,
                       err_t error) {
  auto* state = static_cast<WebSocketState*>(argument);
  if (state == nullptr) {
    if (packet != nullptr) pbuf_free(packet);
    return ERR_ARG;
  }
  if (packet == nullptr) {
    webSocketDisconnect(state, "remote closed");
    return ERR_OK;
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    webSocketDisconnect(state, lwip_strerr(error));
    return error;
  }
  const uint16_t room = sizeof(state->rxBuffer) - state->rxLength;
  const uint16_t copyLength = packet->tot_len < room ? packet->tot_len : room;
  pbuf_copy_partial(packet, state->rxBuffer + state->rxLength, copyLength, 0);
  state->rxLength += copyLength;
  tcp_recved(pcb, packet->tot_len);
  pbuf_free(packet);

  if (state->state == WebSocketClient::HANDSHAKE) {
    state->rxBuffer[state->rxLength < sizeof(state->rxBuffer)
                        ? state->rxLength
                        : sizeof(state->rxBuffer) - 1] = '\0';
    if (strstr(reinterpret_cast<char*>(state->rxBuffer), " 101 ") == nullptr) {
      if (state->rxLength == sizeof(state->rxBuffer)) {
        webSocketDisconnect(state, "handshake rejected");
      }
      return ERR_OK;
    }
    state->state = WebSocketClient::CONNECTED;
    state->rxLength = 0;
    if (state->onConnect != nullptr) state->onConnect();
    return ERR_OK;
  }

  while (state->rxLength >= 2) {
    const uint8_t opcode = state->rxBuffer[0] & 0x0F;
    const bool masked = (state->rxBuffer[1] & 0x80) != 0;
    uint64_t payloadLength = state->rxBuffer[1] & 0x7F;
    uint16_t headerLength = 2;
    if (payloadLength == 126) {
      if (state->rxLength < 4) break;
      payloadLength = (state->rxBuffer[2] << 8) | state->rxBuffer[3];
      headerLength = 4;
    } else if (payloadLength == 127 || payloadLength > 2048) {
      webSocketDisconnect(state, "frame too large");
      return ERR_OK;
    }
    if (masked) headerLength += 4;
    if (state->rxLength < headerLength + payloadLength) break;
    uint8_t* payload = state->rxBuffer + headerLength;
    if (masked) {
      const uint8_t* mask = state->rxBuffer + headerLength - 4;
      for (uint16_t index = 0; index < payloadLength; ++index) {
        payload[index] ^= mask[index % 4];
      }
    }
    if (opcode == 0x8) {
      webSocketDisconnect(state, "close frame");
      return ERR_OK;
    }
    if (opcode == 0x9) {
      webSocketSendFrame(state, 0xA, payload, payloadLength);
    } else if ((opcode == 0x1 || opcode == 0x2) && state->onMessage != nullptr) {
      state->onMessage(payload, payloadLength, opcode == 0x2);
    }
    const uint16_t frameLength = headerLength + payloadLength;
    memmove(state->rxBuffer, state->rxBuffer + frameLength,
            state->rxLength - frameLength);
    state->rxLength -= frameLength;
  }
  return ERR_OK;
}

void webSocketError(void* argument, err_t error) {
  auto* state = static_cast<WebSocketState*>(argument);
  if (state != nullptr) webSocketDisconnect(state, lwip_strerr(error));
}

void wsSha1(const uint8_t* data, uint32_t length, uint8_t output[20]) {
  uint32_t words[80] = {};
  uint32_t hash[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                      0xC3D2E1F0};
  const uint64_t bitLength = static_cast<uint64_t>(length) * 8;
  const uint32_t paddedLength = ((length + 9 + 63) / 64) * 64;
  uint8_t block[128] = {};
  for (uint32_t offset = 0; offset < paddedLength; offset += 64) {
    const uint32_t remaining = length > offset ? length - offset : 0;
    const uint32_t copyLength = remaining > 64 ? 64 : remaining;
    memset(block, 0, sizeof(block));
    if (copyLength > 0) memcpy(block, data + offset, copyLength);
    if (remaining < 64) {
      block[copyLength] = 0x80;
      if (copyLength <= 55) {
        for (uint8_t index = 0; index < 8; ++index) {
          block[63 - index] = static_cast<uint8_t>(bitLength >> (index * 8));
        }
      }
    }
    for (uint8_t index = 0; index < 16; ++index) {
      words[index] = (block[index * 4] << 24) |
                     (block[index * 4 + 1] << 16) |
                     (block[index * 4 + 2] << 8) | block[index * 4 + 3];
    }
    for (uint8_t index = 16; index < 80; ++index) {
      const uint32_t value = words[index - 3] ^ words[index - 8] ^
                             words[index - 14] ^ words[index - 16];
      words[index] = (value << 1) | (value >> 31);
    }
    uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];
    for (uint8_t index = 0; index < 80; ++index) {
      uint32_t function = 0;
      uint32_t constant = 0;
      if (index < 20) {
        function = (b & c) | ((~b) & d);
        constant = 0x5A827999;
      } else if (index < 40) {
        function = b ^ c ^ d;
        constant = 0x6ED9EBA1;
      } else if (index < 60) {
        function = (b & c) | (b & d) | (c & d);
        constant = 0x8F1BBCDC;
      } else {
        function = b ^ c ^ d;
        constant = 0xCA62C1D6;
      }
      const uint32_t rotate = (a << 5) | (a >> 27);
      const uint32_t next = rotate + function + e + constant + words[index];
      e = d;
      d = c;
      c = (b << 30) | (b >> 2);
      b = a;
      a = next;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
  }
  for (uint8_t index = 0; index < 20; ++index) {
    output[index] = static_cast<uint8_t>(hash[index / 4] >>
                                         (24 - (index % 4) * 8));
  }
}

uint16_t wsBase64(const uint8_t* data, uint16_t length, char* output,
                 uint16_t outputSize) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint16_t outputLength = 0;
  for (uint16_t index = 0; index < length; index += 3) {
    if (outputLength + 4 >= outputSize) break;
    const uint32_t value = (static_cast<uint32_t>(data[index]) << 16) |
                           (index + 1 < length ? data[index + 1] << 8 : 0) |
                           (index + 2 < length ? data[index + 2] : 0);
    output[outputLength++] = alphabet[(value >> 18) & 0x3F];
    output[outputLength++] = alphabet[(value >> 12) & 0x3F];
    output[outputLength++] = index + 1 < length ? alphabet[(value >> 6) & 0x3F] : '=';
    output[outputLength++] = index + 2 < length ? alphabet[value & 0x3F] : '=';
  }
  output[outputLength] = '\0';
  return outputLength;
}

bool wsServerSendFrame(WebSocketServerState* state, uint8_t opcode,
                       const uint8_t* data, uint16_t length) {
  if (state == nullptr || state->client == nullptr ||
      !state->handshakeComplete || length > 2048 ||
      (data == nullptr && length != 0)) {
    return false;
  }
  uint8_t frame[2054] = {};
  uint16_t headerLength = 2;
  frame[0] = 0x80 | opcode;
  if (length < 126) {
    frame[1] = length;
  } else {
    frame[1] = 126;
    frame[2] = length >> 8;
    frame[3] = length;
    headerLength = 4;
  }
  memcpy(frame + headerLength, data, length);
  if (tcp_write(state->client, frame, headerLength + length,
                TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
  return tcp_output(state->client) == ERR_OK;
}

err_t webSocketServerReceive(void* argument, tcp_pcb* pcb, pbuf* packet,
                             err_t error);
void webSocketServerError(void* argument, err_t error);

err_t webSocketServerAccept(void* argument, tcp_pcb* pcb, err_t error) {
  auto* server = static_cast<WebSocketServer*>(argument);
  if (server == nullptr || error != ERR_OK) return error;
  auto* state = webSocketServerState(server);
  if (state->client != nullptr) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  state->client = pcb;
  state->handshakeComplete = false;
  state->rxLength = 0;
  tcp_arg(pcb, server);
  tcp_recv(pcb, webSocketServerReceive);
  tcp_err(pcb, webSocketServerError);
  return ERR_OK;
}

err_t webSocketServerReceive(void* argument, tcp_pcb* pcb, pbuf* packet,
                             err_t error) {
  auto* server = static_cast<WebSocketServer*>(argument);
  if (server == nullptr) {
    if (packet != nullptr) pbuf_free(packet);
    return ERR_ARG;
  }
  auto* state = webSocketServerState(server);
  if (packet == nullptr) {
    state->client = nullptr;
    return tcp_close(pcb);
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    return error;
  }
  const uint16_t room = sizeof(state->rxBuffer) - state->rxLength;
  const uint16_t copyLength = packet->tot_len < room ? packet->tot_len : room;
  pbuf_copy_partial(packet, state->rxBuffer + state->rxLength, copyLength, 0);
  state->rxLength += copyLength;
  tcp_recved(pcb, packet->tot_len);
  pbuf_free(packet);

  if (!state->handshakeComplete) {
    state->rxBuffer[state->rxLength < sizeof(state->rxBuffer)
                        ? state->rxLength
                        : sizeof(state->rxBuffer) - 1] = '\0';
    char* key = strstr(reinterpret_cast<char*>(state->rxBuffer),
                       "Sec-WebSocket-Key: ");
    char* end = key == nullptr ? nullptr : strstr(key, "\r\n");
    if (key == nullptr || end == nullptr) return ERR_OK;
    key += strlen("Sec-WebSocket-Key: ");
    char acceptInput[128] = {};
    const size_t keyLength = end - key;
    if (keyLength >= sizeof(acceptInput) - 37) return ERR_VAL;
    memcpy(acceptInput, key, keyLength);
    strcpy(acceptInput + keyLength, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    uint8_t digest[20] = {};
    char accept[32] = {};
    wsSha1(reinterpret_cast<uint8_t*>(acceptInput), strlen(acceptInput), digest);
    wsBase64(digest, sizeof(digest), accept, sizeof(accept));
    char response[192] = {};
    const int responseLength = snprintf(
        response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    tcp_write(pcb, response, responseLength, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    state->handshakeComplete = true;
    state->rxLength = 0;
    return ERR_OK;
  }

  while (state->rxLength >= 2) {
    const uint8_t opcode = state->rxBuffer[0] & 0x0F;
    const bool masked = (state->rxBuffer[1] & 0x80) != 0;
    uint16_t payloadLength = state->rxBuffer[1] & 0x7F;
    uint16_t headerLength = 2;
    if (payloadLength == 126) {
      if (state->rxLength < 4) break;
      payloadLength = (state->rxBuffer[2] << 8) | state->rxBuffer[3];
      headerLength = 4;
    }
    if (!masked || payloadLength > 2048) return ERR_VAL;
    headerLength += 4;
    if (state->rxLength < headerLength + payloadLength) break;
    uint8_t* payload = state->rxBuffer + headerLength;
    const uint8_t* mask = state->rxBuffer + headerLength - 4;
    for (uint16_t index = 0; index < payloadLength; ++index) {
      payload[index] ^= mask[index % 4];
    }
    if (opcode == 0x9) {
      wsServerSendFrame(state, 0xA, payload, payloadLength);
    } else if (opcode == 0x8) {
      state->client = nullptr;
      tcp_close(pcb);
      return ERR_OK;
    } else if ((opcode == 0x1 || opcode == 0x2) && state->callback != nullptr) {
      state->callback(payload, payloadLength, opcode == 0x2);
    }
    const uint16_t frameLength = headerLength + payloadLength;
    memmove(state->rxBuffer, state->rxBuffer + frameLength,
            state->rxLength - frameLength);
    state->rxLength -= frameLength;
  }
  return ERR_OK;
}

void webSocketServerError(void* argument, err_t error) {
  auto* server = static_cast<WebSocketServer*>(argument);
  if (server == nullptr) return;
  auto* state = webSocketServerState(server);
  state->client = nullptr;
  state->handshakeComplete = false;
  state->rxLength = 0;
  (void)error;
}

}  // namespace

UdpSocket::~UdpSocket() {
  stop();
  delete udpState(this);
}

bool UdpSocket::begin(uint16_t port) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<State*>(new UdpState());
  }
  auto* state = udpState(this);
  stop();
  state->pcb = udp_new();
  if (state->pcb == nullptr || udp_bind(state->pcb, IP4_ADDR_ANY, port) !=
                                  ERR_OK) {
    stop();
    return false;
  }
  udp_recv(state->pcb, udpReceive, state);
  return true;
}

void UdpSocket::stop() {
  auto* state = udpState(this);
  if (state != nullptr && state->pcb != nullptr) {
    udp_remove(state->pcb);
    state->pcb = nullptr;
  }
}

bool UdpSocket::beginPacket(const ip4_addr_t& address, uint16_t port) {
  auto* state = udpState(this);
  if (state == nullptr || state->pcb == nullptr || port == 0) {
    return false;
  }
  state->destination = address;
  state->destinationPort = port;
  state->txLength = 0;
  return true;
}

size_t UdpSocket::write(const uint8_t* data, size_t length) {
  auto* state = udpState(this);
  if (state == nullptr || data == nullptr || length > UDP_BUFFER_SIZE -
                                                   state->txLength) {
    return 0;
  }
  memcpy(state->txBuffer + state->txLength, data, length);
  state->txLength += length;
  return length;
}

size_t UdpSocket::write(uint8_t value) { return write(&value, 1); }

bool UdpSocket::endPacket() {
  auto* state = udpState(this);
  if (state == nullptr || state->pcb == nullptr) {
    return false;
  }
  pbuf* packet = pbuf_alloc(PBUF_TRANSPORT, state->txLength, PBUF_RAM);
  if (packet == nullptr) {
    return false;
  }
  memcpy(packet->payload, state->txBuffer, state->txLength);
  const err_t result = udp_sendto(state->pcb, packet, &state->destination,
                                  state->destinationPort);
  pbuf_free(packet);
  state->txLength = 0;
  return result == ERR_OK;
}

int UdpSocket::parsePacket() {
  auto* state = udpState(this);
  return state == nullptr ? 0 : state->rxLength - state->rxOffset;
}

int UdpSocket::read(uint8_t* data, size_t length) {
  auto* state = udpState(this);
  if (state == nullptr || data == nullptr) {
    return 0;
  }
  const size_t count = length < parsePacket() ? length : parsePacket();
  memcpy(data, state->rxBuffer + state->rxOffset, count);
  state->rxOffset += count;
  if (state->rxOffset == state->rxLength) {
    state->rxOffset = 0;
    state->rxLength = 0;
  }
  return count;
}

int UdpSocket::available() const {
  auto* state = reinterpret_cast<const UdpState*>(state_);
  return state == nullptr ? 0 : state->rxLength - state->rxOffset;
}

const ip4_addr_t& UdpSocket::remoteAddress() const {
  return reinterpret_cast<const UdpState*>(state_)->remote;
}

uint16_t UdpSocket::remotePort() const {
  return reinterpret_cast<const UdpState*>(state_)->remotePort;
}

void UdpSocket::onPacket(PacketCallback callback) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<State*>(new UdpState());
  }
  udpState(this)->callback = callback;
}

TcpClient::TcpClient() { state_ = reinterpret_cast<StateData*>(new TcpState()); }

TcpClient::~TcpClient() {
  stop();
  delete tcpState(this);
}

bool TcpClient::connect(const ip4_addr_t& address, uint16_t port) {
  auto* state = tcpState(this);
  stop();
  state->pcb = tcp_new();
  if (state->pcb == nullptr) {
    return false;
  }
  state->state = CONNECTING;
  tcp_arg(state->pcb, state);
  tcp_recv(state->pcb, tcpReceive);
  tcp_err(state->pcb, tcpError);
  return tcp_connect(state->pcb, &address, port, tcpConnected) == ERR_OK;
}

void TcpClient::stop() {
  auto* state = tcpState(this);
  if (state == nullptr || state->pcb == nullptr) {
    return;
  }
  tcp_pcb* pcb = state->pcb;
  if (state->closePending &&
      (pcb->unsent != nullptr || pcb->unacked != nullptr)) {
    return;
  }
  tcp_arg(pcb, nullptr);
  if (state->closePending) {
    tcp_abort(pcb);
    state->pcb = nullptr;
    state->state = IDLE;
    state->closePending = false;
    return;
  }
  if (tcp_close(pcb) != ERR_OK) {
    tcp_arg(pcb, state);
    return;
  }
  state->pcb = nullptr;
  state->state = IDLE;
  state->closePending = false;
}

size_t TcpClient::write(const uint8_t* data, size_t length) {
  auto* state = tcpState(this);
  if (state == nullptr || state->pcb == nullptr || state->state != CONNECTED ||
      data == nullptr || length > tcp_sndbuf(state->pcb)) {
    return 0;
  }
  if (tcp_write(state->pcb, data, length, TCP_WRITE_FLAG_COPY) != ERR_OK) {
    return 0;
  }
  tcp_output(state->pcb);
  return length;
}

size_t TcpClient::write(uint8_t value) { return write(&value, 1); }

size_t TcpClient::write(const char* text) {
  return text == nullptr ? 0 : print(text);
}

size_t TcpClient::write(const char* text, size_t length) {
  return text == nullptr
             ? 0
             : write(reinterpret_cast<const uint8_t*>(text), length);
}

size_t TcpClient::print(const char* text) {
  return text == nullptr ? 0 : write(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

size_t TcpClient::print(unsigned long value, int base) {
  char buffer[24] = {};
  if (base == 16) {
    snprintf(buffer, sizeof(buffer), "%lx", value);
  } else {
    snprintf(buffer, sizeof(buffer), "%lu", value);
  }
  return print(buffer);
}

int TcpClient::printf(const char* format, ...) {
  if (format == nullptr) return 0;
  char buffer[256] = {};
  va_list arguments;
  va_start(arguments, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  if (length <= 0) return length;
  size_t outputLength = static_cast<size_t>(length);
  if (outputLength >= sizeof(buffer)) outputLength = sizeof(buffer) - 1;
  return static_cast<int>(write(reinterpret_cast<const uint8_t*>(buffer), outputLength));
}

int TcpClient::peek() const {
  auto* state = reinterpret_cast<const TcpState*>(state_);
  return available() == 0 ? -1 : state->rxBuffer[state->rxTail];
}

void TcpClient::flush() {
  auto* state = tcpState(this);
  if (state != nullptr && state->pcb != nullptr) tcp_output(state->pcb);
}

void TcpClient::setNoDelay(bool enabled) {
  auto* state = tcpState(this);
  if (state == nullptr || state->pcb == nullptr) return;
  if (enabled) {
    tcp_nagle_disable(state->pcb);
  } else {
    tcp_nagle_enable(state->pcb);
  }
}

int TcpClient::available() const {
  auto* state = reinterpret_cast<const TcpState*>(state_);
  return state == nullptr
             ? 0
             : (state->rxHead + TCP_BUFFER_SIZE - state->rxTail) %
                   TCP_BUFFER_SIZE;
}

int TcpClient::read() {
  uint8_t value = 0;
  return read(&value, 1) == 1 ? value : -1;
}

int TcpClient::read(uint8_t* data, size_t length) {
  auto* state = tcpState(this);
  if (state == nullptr || data == nullptr) {
    return 0;
  }
  size_t count = length < available() ? length : available();
  for (size_t index = 0; index < count; ++index) {
    data[index] = state->rxBuffer[state->rxTail];
    state->rxTail = (state->rxTail + 1) % TCP_BUFFER_SIZE;
  }
  return count;
}

bool TcpClient::connected() const {
  auto* state = reinterpret_cast<const TcpState*>(state_);
  return state != nullptr && state->state == CONNECTED;
}

TcpClient::State TcpClient::state() const {
  auto* state = reinterpret_cast<const TcpState*>(state_);
  return state == nullptr ? IDLE : state->state;
}

void TcpClient::onConnect(ConnectCallback callback) {
  tcpState(this)->onConnect = callback;
}

void TcpClient::onDisconnect(DisconnectCallback callback) {
  tcpState(this)->onDisconnect = callback;
}

void TcpClient::onData(DataCallback callback) {
  tcpState(this)->onData = callback;
}

bool TcpClient::adoptAccepted(void* nativePcb) {
  auto* state = tcpState(this);
  auto* pcb = static_cast<tcp_pcb*>(nativePcb);
  if (state == nullptr || pcb == nullptr) return false;
  stop();
  state->rxHead = 0;
  state->rxTail = 0;
  state->pcb = pcb;
  state->state = CONNECTED;
  state->closePending = false;
  tcp_arg(pcb, state);
  tcp_recv(pcb, tcpReceive);
  tcp_err(pcb, tcpError);
  return true;
}

TcpServer::~TcpServer() {
  stop();
  delete tcpServerState(this);
}

bool TcpServer::begin(uint16_t port) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<StateData*>(new TcpServerState());
  }
  auto* state = tcpServerState(this);
  stop();
  state->listener = tcp_new();
  if (state->listener == nullptr ||
      tcp_bind(state->listener, IP4_ADDR_ANY, port) != ERR_OK) {
    stop();
    return false;
  }
  state->listener = tcp_listen_with_backlog(state->listener, 1);
  if (state->listener == nullptr) return false;
  tcp_arg(state->listener, state);
  tcp_accept(state->listener, tcpServerAccept);
  return true;
}

void TcpServer::stop() {
  auto* state = tcpServerState(this);
  if (state == nullptr) return;
  state->client.stop();
  if (state->listener != nullptr) {
    tcp_arg(state->listener, nullptr);
    if (tcp_close(state->listener) != ERR_OK) tcp_abort(state->listener);
    state->listener = nullptr;
  }
}

TcpClient* TcpServer::available() {
  auto* state = tcpServerState(this);
  if (state == nullptr) return nullptr;
  auto* clientState = tcpState(&state->client);
  if (clientState != nullptr && clientState->closePending) {
    state->client.stop();
  }
  if (!state->client.connected() && state->client.available() == 0) {
    return nullptr;
  }
  return &state->client;
}

bool HttpServer::begin(uint16_t port) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<StateData*>(new HttpState());
  }
  auto* state = httpState(this);
  stop();
  state->listener = tcp_new();
  if (state->listener == nullptr ||
      tcp_bind(state->listener, IP4_ADDR_ANY, port) != ERR_OK) {
    stop();
    return false;
  }
  state->listener = tcp_listen_with_backlog(state->listener, 2);
  if (state->listener == nullptr) {
    return false;
  }
  tcp_arg(state->listener, this);
  tcp_accept(state->listener, httpAccept);
  return true;
}

HttpServer::~HttpServer() {
  stop();
  delete httpState(this);
}

void HttpServer::stop() {
  auto* state = httpState(this);
  if (state == nullptr) return;
  if (state->client != nullptr) {
    tcp_arg(state->client, nullptr);
    tcp_abort(state->client);
    state->client = nullptr;
  }
  if (state->listener != nullptr) {
    tcp_arg(state->listener, nullptr);
    tcp_close(state->listener);
    state->listener = nullptr;
  }
}

void HttpServer::poll() {}

void HttpServer::get(const char* uri, EndpointHandler handler) {
  auto* state = httpState(this);
  if (state->routeCount >= HTTP_MAX_ROUTES) return;
  state->routes[state->routeCount++] = {uri, HTTP_GET, handler};
}

void HttpServer::post(const char* uri, EndpointHandler handler) {
  auto* state = httpState(this);
  if (state->routeCount >= HTTP_MAX_ROUTES) return;
  state->routes[state->routeCount++] = {uri, HTTP_POST, handler};
}

void HttpServer::onNotFound(EndpointHandler handler) {
  httpState(this)->notFound = handler;
}

void HttpServer::send(int code, const char* contentType, const char* content) {
  send(code, contentType, content,
       content == nullptr ? 0 : static_cast<int>(strlen(content)));
}

void HttpServer::send(int code, const char* contentType, const char* content,
                      int length) {
  auto* state = httpState(this);
  sendHeader(code, contentType, length);
  if (content != nullptr && length > 0) write(
      reinterpret_cast<const uint8_t*>(content), length);
  end();
  state->responseLength = 0;
}

void HttpServer::sendBuffer(int code, const uint8_t* data, int length) {
  sendHeader(code, "application/octet-stream", length);
  if (data != nullptr && length > 0) write(data, length);
  end();
}

void HttpServer::sendHeader(int code, const char* contentType, int length) {
  auto* state = httpState(this);
  state->responseCode = code;
  state->responseType = contentType == nullptr ? "text/plain" : contentType;
  state->responseLength = 0;
  const int headerLength = snprintf(
      state->response, sizeof(state->response),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
      "Connection: close\r\n\r\n",
      code, httpReason(code), state->responseType, length < 0 ? 0 : length);
  state->responseLength = headerLength > 0 ? headerLength : 0;
}

void HttpServer::write(const uint8_t* data, int length) {
  auto* state = httpState(this);
  if (data == nullptr || length <= 0) return;
  const uint16_t room = sizeof(state->response) - state->responseLength;
  const uint16_t copyLength = length < room ? length : room;
  memcpy(state->response + state->responseLength, data, copyLength);
  state->responseLength += copyLength;
}

void HttpServer::end() {
  auto* state = httpState(this);
  if (state->client == nullptr || state->responseLength == 0) return;
  tcp_write(state->client, state->response, state->responseLength,
            TCP_WRITE_FLAG_COPY);
  tcp_output(state->client);
  tcp_arg(state->client, nullptr);
  tcp_close(state->client);
  state->client = nullptr;
}

const char* HttpServer::uri() const { return httpState(const_cast<HttpServer*>(this))->uri; }

HttpMethod HttpServer::method() const {
  return httpState(const_cast<HttpServer*>(this))->method;
}

const char* HttpServer::body() const {
  return httpState(const_cast<HttpServer*>(this))->body;
}

bool SntpClient::begin(const ip4_addr_t& server, uint16_t port) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<StateData*>(new SntpState());
  }
  auto* state = sntpState(this);
  registerSntpClient(this);
  state->socket.stop();
  state->waiting = false;
  state->synchronized = false;
  if (!state->socket.begin(60000)) return false;
  uint8_t request[48] = {};
  request[0] = 0x23;
  if (!state->socket.beginPacket(server, port) ||
      state->socket.write(request, sizeof(request)) != sizeof(request) ||
      !state->socket.endPacket()) {
    state->socket.stop();
    return false;
  }
  state->waiting = true;
  return true;
}

SntpClient::~SntpClient() {
  unregisterSntpClient(this);
  stop();
  delete sntpState(this);
}

void SntpClient::stop() {
  auto* state = sntpState(this);
  if (state == nullptr) return;
  state->socket.stop();
  state->waiting = false;
}

void SntpClient::poll() {
  auto* state = sntpState(this);
  if (state == nullptr || !state->waiting || state->socket.parsePacket() < 48) {
    return;
  }
  uint8_t response[48] = {};
  if (state->socket.read(response, sizeof(response)) != sizeof(response)) {
    stop();
    return;
  }
  state->socket.stop();
  state->waiting = false;
  if ((response[0] & 0x07) != 4 && (response[0] & 0x07) != 5) return;
  uint32_t ntpSeconds = 0;
  for (uint8_t index = 40; index < 44; ++index) {
    ntpSeconds = (ntpSeconds << 8) | response[index];
  }
  if (ntpSeconds < 2208988800UL) return;
  state->unixSeconds = ntpSeconds - 2208988800UL;
  state->synchronized = true;
  if (state->callback != nullptr) state->callback(state->unixSeconds);
}

bool SntpClient::waiting() const {
  auto* state = reinterpret_cast<const SntpState*>(state_);
  return state != nullptr && state->waiting;
}

bool SntpClient::synchronized() const {
  auto* state = reinterpret_cast<const SntpState*>(state_);
  return state != nullptr && state->synchronized;
}

uint32_t SntpClient::unixSeconds() const {
  auto* state = reinterpret_cast<const SntpState*>(state_);
  return state == nullptr ? 0 : state->unixSeconds;
}

void SntpClient::onTime(TimeCallback callback) {
  if (state_ == nullptr) state_ = reinterpret_cast<StateData*>(new SntpState());
  sntpState(this)->callback = callback;
}

WebSocketClient::WebSocketClient() {
  state_ = reinterpret_cast<StateData*>(new WebSocketState());
}

WebSocketClient::~WebSocketClient() {
  close();
  delete webSocketState(this);
}

bool WebSocketClient::setUrl(const char* url) {
  if (url == nullptr || strncmp(url, "ws://", 5) != 0) return false;
  const char* hostStart = url + 5;
  const char* pathStart = strchr(hostStart, '/');
  const char* portStart = strchr(hostStart, ':');
  const char* hostEnd = pathStart == nullptr ? url + strlen(url) : pathStart;
  if (portStart != nullptr && portStart < hostEnd) hostEnd = portStart;
  const size_t hostLength = hostEnd - hostStart;
  auto* state = webSocketState(this);
  if (hostLength == 0 || hostLength >= sizeof(state->host)) return false;
  memcpy(state->host, hostStart, hostLength);
  state->host[hostLength] = '\0';
  state->port = 80;
  if (portStart != nullptr && portStart < (pathStart == nullptr ? url + strlen(url) : pathStart)) {
    state->port = static_cast<uint16_t>(atoi(portStart + 1));
  }
  if (pathStart != nullptr) {
    strncpy(state->path, pathStart, sizeof(state->path) - 1);
    state->path[sizeof(state->path) - 1] = '\0';
  } else {
    strcpy(state->path, "/");
  }
  return ip4addr_aton(state->host, &state->address) != 0;
}

void WebSocketClient::clearUrl() {
  auto* state = webSocketState(this);
  close();
  state->host[0] = '\0';
  state->path[0] = '/';
  state->path[1] = '\0';
}

bool WebSocketClient::connect() {
  auto* state = webSocketState(this);
  close();
  if (state->host[0] == '\0') return false;
  state->pcb = tcp_new();
  if (state->pcb == nullptr) return false;
  state->state = CONNECTING;
  tcp_arg(state->pcb, state);
  tcp_recv(state->pcb, webSocketReceive);
  tcp_err(state->pcb, webSocketError);
  if (tcp_connect(state->pcb, &state->address, state->port,
                  webSocketConnected) != ERR_OK) {
    close();
    return false;
  }
  return true;
}

void WebSocketClient::close() {
  auto* state = webSocketState(this);
  if (state != nullptr && state->state != DISCONNECTED) {
    webSocketDisconnect(state, "closed");
  }
}

void WebSocketClient::poll() {}

bool WebSocketClient::sendText(const char* text) {
  return text != nullptr && webSocketSendFrame(
      webSocketState(this), 0x1, reinterpret_cast<const uint8_t*>(text),
      strlen(text));
}

bool WebSocketClient::sendBinary(const void* data, uint16_t length) {
  return webSocketSendFrame(webSocketState(this), 0x2,
                            static_cast<const uint8_t*>(data), length);
}

bool WebSocketClient::isConnected() const {
  auto* state = reinterpret_cast<const WebSocketState*>(state_);
  return state != nullptr && state->state == CONNECTED;
}

WebSocketClient::State WebSocketClient::state() const {
  auto* state = reinterpret_cast<const WebSocketState*>(state_);
  return state == nullptr ? DISCONNECTED : state->state;
}

uint16_t WebSocketClient::port() const {
  auto* state = reinterpret_cast<const WebSocketState*>(state_);
  return state == nullptr ? 0 : state->port;
}

void WebSocketClient::onConnect(ConnectCallback callback) {
  webSocketState(this)->onConnect = callback;
}

void WebSocketClient::onDisconnect(DisconnectCallback callback) {
  webSocketState(this)->onDisconnect = callback;
}

void WebSocketClient::onMessage(WebSocketMessageCallback callback) {
  webSocketState(this)->onMessage = callback;
}

bool WebSocketServer::begin(uint16_t port) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<StateData*>(new WebSocketServerState());
  }
  auto* state = webSocketServerState(this);
  stop();
  state->listener = tcp_new();
  if (state->listener == nullptr ||
      tcp_bind(state->listener, IP4_ADDR_ANY, port) != ERR_OK) {
    stop();
    return false;
  }
  state->listener = tcp_listen_with_backlog(state->listener, 1);
  if (state->listener == nullptr) return false;
  tcp_arg(state->listener, this);
  tcp_accept(state->listener, webSocketServerAccept);
  return true;
}

WebSocketServer::~WebSocketServer() {
  stop();
  delete webSocketServerState(this);
}

void WebSocketServer::stop() {
  auto* state = webSocketServerState(this);
  if (state == nullptr) return;
  if (state->client != nullptr) {
    tcp_arg(state->client, nullptr);
    tcp_abort(state->client);
    state->client = nullptr;
  }
  if (state->listener != nullptr) {
    tcp_arg(state->listener, nullptr);
    tcp_close(state->listener);
    state->listener = nullptr;
  }
  state->handshakeComplete = false;
  state->rxLength = 0;
}

void WebSocketServer::loop() {}

void WebSocketServer::setMessageHandler(MessageCallback callback) {
  if (state_ == nullptr) {
    state_ = reinterpret_cast<StateData*>(new WebSocketServerState());
  }
  webSocketServerState(this)->callback = callback;
}

bool WebSocketServer::broadcast(const void* data, uint16_t length,
                                bool binary) {
  if (data == nullptr && length != 0) return false;
  auto* state = webSocketServerState(this);
  return wsServerSendFrame(state, binary ? 0x2 : 0x1,
                           static_cast<const uint8_t*>(data), length);
}

bool WebSocketServer::emit(const char* topic, const void* data, uint16_t length,
                           bool binary) {
  (void)topic;
  return broadcast(data, length, binary);
}

void poll() {
  for (uint8_t index = 0; index < SNTP_CLIENT_MAX; ++index) {
    if (sntpClients[index] != nullptr) sntpClients[index]->poll();
  }
}

}  // namespace xtp_lwip