#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SOCKET = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace omnigpu::tcp {

bool init();
void cleanup();

void shutdown_socket(SOCKET fd);
void close_socket(SOCKET fd);
bool set_tcp_nodelay(SOCKET fd);

bool send_all(SOCKET fd, const uint8_t* data, size_t size);
bool recv_all(SOCKET fd, uint8_t* buffer, size_t size);
// Receive the complete buffer before a single overall deadline. Unlike a
// socket inactivity timeout, a slow-drip peer cannot extend this indefinitely.
bool recv_all_for(SOCKET fd, uint8_t* buffer, size_t size,
                  uint32_t timeout_ms);

// Set send and receive socket timeouts (seconds). 0 = no timeout.
bool set_tcp_timeout(SOCKET fd, uint32_t timeout_s);

std::string last_error();

} // namespace omnigpu::tcp
