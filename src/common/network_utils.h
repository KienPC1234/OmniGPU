#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
using SOCKET = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace omnigpu::tcp {

bool init();
void cleanup();

void close_socket(SOCKET fd);
bool set_tcp_nodelay(SOCKET fd);

bool send_all(SOCKET fd, const uint8_t* data, size_t size);
bool recv_all(SOCKET fd, uint8_t* buffer, size_t size);

// Set socket timeout (seconds). 0 = no timeout.
bool set_tcp_timeout(SOCKET fd, uint32_t timeout_s);

// Enable TCP keepalive with 10s idle, 3s interval, 3 probes.
bool set_tcp_keepalive(SOCKET fd);

std::string last_error();

} // namespace omnigpu::tcp
