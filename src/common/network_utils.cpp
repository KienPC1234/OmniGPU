#include "network_utils.h"
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu::tcp {

#ifdef _WIN32
bool init() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        SPDLOG_ERROR("WSAStartup failed: {}", last_error());
        return false;
    }
    return true;
}

void cleanup() { WSACleanup(); }

void close_socket(SOCKET fd) { closesocket(fd); }

std::string last_error() {
    int err = WSAGetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::to_string(err) + ": " + buf;
}
#else
bool init() { return true; }

void cleanup() {}

void close_socket(SOCKET fd) { ::close(fd); }

std::string last_error() { return std::string(std::strerror(errno)); }
#endif

bool set_tcp_nodelay(SOCKET fd) {
    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag)) != 0) {
        SPDLOG_ERROR("TCP_NODELAY failed: {}", last_error());
        return false;
    }
    return true;
}

bool send_all(SOCKET fd, const uint8_t* data, size_t size) {
    while (size > 0) {
#ifdef _WIN32
        int sent = ::send(fd, reinterpret_cast<const char*>(data),
                          static_cast<int>(size), 0);
#else
        auto sent = ::send(fd, data, size, 0);
#endif
        if (sent <= 0) {
            SPDLOG_ERROR("send failed: {}", last_error());
            return false;
        }
        data += static_cast<size_t>(sent);
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool recv_all(SOCKET fd, uint8_t* buffer, size_t size) {
    while (size > 0) {
#ifdef _WIN32
        int received = ::recv(fd, reinterpret_cast<char*>(buffer),
                              static_cast<int>(size), 0);
#else
        auto received = ::recv(fd, buffer, size, 0);
#endif
        if (received <= 0) {
            if (received == 0) {
                SPDLOG_ERROR("recv: connection closed by peer");
            } else {
                SPDLOG_ERROR("recv failed: {}", last_error());
            }
            return false;
        }
        buffer += static_cast<size_t>(received);
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool set_tcp_timeout(SOCKET fd, uint32_t timeout_s) {
    if (timeout_s == 0) return true;
#ifdef _WIN32
    DWORD tv = timeout_s * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv)) != 0) {
        SPDLOG_ERROR("SO_RCVTIMEO failed: {}", last_error());
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        SPDLOG_ERROR("SO_RCVTIMEO failed: {}", last_error());
        return false;
    }
#endif
    return true;
}

} // namespace omnigpu::tcp
