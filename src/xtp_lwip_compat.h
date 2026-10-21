#pragma once
#ifndef XTP_LWIP_COMPAT_H
#define XTP_LWIP_COMPAT_H

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include <lwip/ip4_addr.h>
}

namespace xtp_lwip {

using DataCallback = void (*)(const uint8_t* data, uint16_t length);
using ConnectCallback = void (*)();
using DisconnectCallback = void (*)(const char* reason);
using TimeCallback = void (*)(uint32_t unixSeconds);
using WebSocketMessageCallback = void (*)(const uint8_t* data, uint16_t length,
                                          bool binary);

class UdpSocket {
 public:
  using PacketCallback = void (*)(const ip4_addr_t& address, uint16_t port,
                                  const uint8_t* data, uint16_t length);

  UdpSocket() = default;
  ~UdpSocket();

  bool begin(uint16_t port);
  void stop();
  bool beginPacket(const ip4_addr_t& address, uint16_t port);
  size_t write(const uint8_t* data, size_t length);
  size_t write(uint8_t value);
  bool endPacket();
  int parsePacket();
  int read(uint8_t* data, size_t length);
  int available() const;
  const ip4_addr_t& remoteAddress() const;
  uint16_t remotePort() const;
  void onPacket(PacketCallback callback);
  void* nativeState() const { return state_; }

 private:
  struct State;
  State* state_ = nullptr;
};

class TcpClient {
 public:
  enum State : uint8_t {
    IDLE = 0,
    CONNECTING,
    CONNECTED,
    CLOSING,
  };

  TcpClient();
  ~TcpClient();

  bool connect(const ip4_addr_t& address, uint16_t port);
  void stop();
  size_t write(const uint8_t* data, size_t length);
  size_t write(uint8_t value);
  size_t write(const char* text);
  size_t write(const char* text, size_t length);
  size_t print(const char* text);
  size_t print(unsigned long value, int base = 10);
  int printf(const char* format, ...);
  int peek() const;
  void flush();
  void setNoDelay(bool enabled);
  int available() const;
  int read();
  int read(uint8_t* data, size_t length);
  bool connected() const;
  State state() const;
  void onConnect(ConnectCallback callback);
  void onDisconnect(DisconnectCallback callback);
  void onData(DataCallback callback);
  bool adoptAccepted(void* nativePcb);
  void* nativeState() const { return state_; }

 private:
  struct StateData;
  StateData* state_ = nullptr;
};

class TcpServer {
 public:
  TcpServer() = default;
  ~TcpServer();

  bool begin(uint16_t port);
  void stop();
  TcpClient* available();
  void* nativeState() const { return state_; }

 private:
  struct StateData;
  StateData* state_ = nullptr;
};

enum HttpMethod : uint8_t {
  HTTP_GET = 0,
  HTTP_POST,
};

class HttpServer {
 public:
  using EndpointHandler = void (*)();

  HttpServer() = default;
  ~HttpServer();

  bool begin(uint16_t port = 80);
  void stop();
  void poll();
  void get(const char* uri, EndpointHandler handler);
  void post(const char* uri, EndpointHandler handler);
  void onNotFound(EndpointHandler handler);

  void send(int code, const char* contentType, const char* content);
  void send(int code, const char* contentType, const char* content,
            int length);
  void sendBuffer(int code, const uint8_t* data, int length);
  void sendHeader(int code, const char* contentType, int length = -1);
  void write(const uint8_t* data, int length);
  void end();

  const char* uri() const;
  HttpMethod method() const;
  const char* body() const;

  void* nativeState() const { return state_; }

 private:
  struct StateData;
  StateData* state_ = nullptr;
};

class SntpClient {
 public:
  SntpClient() = default;
  ~SntpClient();

  bool begin(const ip4_addr_t& server, uint16_t port = 123);
  void stop();
  void poll();
  bool waiting() const;
  bool synchronized() const;
  uint32_t unixSeconds() const;
  void onTime(TimeCallback callback);
  void* nativeState() const { return state_; }

 private:
  struct StateData;
  StateData* state_ = nullptr;
};

class WebSocketClient {
 public:
  enum State : uint8_t {
    DISCONNECTED = 0,
    CONNECTING,
    HANDSHAKE,
    CONNECTED,
  };

  WebSocketClient();
  ~WebSocketClient();

  bool setUrl(const char* url);
  void clearUrl();
  bool connect();
  void close();
  void poll();
  bool sendText(const char* text);
  bool sendBinary(const void* data, uint16_t length);
  bool isConnected() const;
  State state() const;
  uint16_t port() const;
  void onConnect(ConnectCallback callback);
  void onDisconnect(DisconnectCallback callback);
  void onMessage(WebSocketMessageCallback callback);
  void* nativeState() const { return state_; }

 private:
  struct StateData;
  StateData* state_ = nullptr;
};

class WebSocketServer {
 public:
  using MessageCallback = void (*)(const uint8_t* data, uint16_t length,
                                   bool binary);

  WebSocketServer() = default;
  ~WebSocketServer();

  bool begin(uint16_t port = 81);
  void stop();
  void loop();
  void setMessageHandler(MessageCallback callback);
  bool broadcast(const void* data, uint16_t length, bool binary = false);
  bool emit(const char* topic, const void* data, uint16_t length,
            bool binary = false);
  void* nativeState() const { return state_; }

 private:
  struct StateData;
  StateData* state_ = nullptr;
};

using XtpTcpClient = TcpClient;
using XtpWsClient = WebSocketClient;
using RestServer = HttpServer;

void poll();

}  // namespace xtp_lwip

#endif  // XTP_LWIP_COMPAT_H