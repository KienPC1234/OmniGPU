#include "network_utils.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

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

void shutdown_socket(SOCKET fd) {
    if (fd != INVALID_SOCKET) ::shutdown(fd, SD_BOTH);
}

void close_socket(SOCKET fd) {
    if (fd != INVALID_SOCKET) closesocket(fd);
}

std::string last_error() {
    const int err = WSAGetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::to_string(err) + ": " + buf;
}
#else
bool init() { return true; }

void cleanup() {}

void shutdown_socket(SOCKET fd) {
    if (fd == INVALID_SOCKET) return;
    while (::shutdown(fd, SHUT_RDWR) != 0 && errno == EINTR) {
    }
}

void close_socket(SOCKET fd) {
    if (fd != INVALID_SOCKET) ::close(fd);
}

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
    if (size != 0 && data == nullptr) {
        SPDLOG_ERROR("send_all called with null data and non-zero size");
        return false;
    }
    while (size > 0) {
#ifdef _WIN32
        const auto chunk = static_cast<int>(std::min<size_t>(
            size, static_cast<size_t>(std::numeric_limits<int>::max())));
        const int sent = ::send(fd, reinterpret_cast<const char*>(data), chunk, 0);
        if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
#else
        const auto max_chunk = static_cast<size_t>(
            std::numeric_limits<ssize_t>::max());
        const size_t chunk = std::min(size, max_chunk);
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const ssize_t sent = ::send(fd, data, chunk, flags);
        if (sent < 0 && errno == EINTR) continue;
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
    if (size != 0 && buffer == nullptr) {
        SPDLOG_ERROR("recv_all called with null buffer and non-zero size");
        return false;
    }
    while (size > 0) {
#ifdef _WIN32
        const auto chunk = static_cast<int>(std::min<size_t>(
            size, static_cast<size_t>(std::numeric_limits<int>::max())));
        const int received = ::recv(fd, reinterpret_cast<char*>(buffer), chunk, 0);
        if (received == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
#else
        const auto max_chunk = static_cast<size_t>(
            std::numeric_limits<ssize_t>::max());
        const size_t chunk = std::min(size, max_chunk);
        const ssize_t received = ::recv(fd, buffer, chunk, 0);
        if (received < 0 && errno == EINTR) continue;
#endif
        if (received <= 0) {
            if (received == 0) {
                SPDLOG_DEBUG("recv: connection closed by peer");
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
#ifdef _WIN32
    const uint64_t requested_ms = static_cast<uint64_t>(timeout_s) * 1000ULL;
    const DWORD timeout_ms = requested_ms > std::numeric_limits<DWORD>::max()
        ? std::numeric_limits<DWORD>::max()
        : static_cast<DWORD>(requested_ms);
    const char* value = reinterpret_cast<const char*>(&timeout_ms);
    const int value_size = sizeof(timeout_ms);
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(timeout_s);
    timeout.tv_usec = 0;
    const void* value = &timeout;
    const socklen_t value_size = sizeof(timeout);
#endif

    bool ok = true;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(value), value_size) != 0) {
        SPDLOG_ERROR("SO_RCVTIMEO failed: {}", last_error());
        ok = false;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(value), value_size) != 0) {
        SPDLOG_ERROR("SO_SNDTIMEO failed: {}", last_error());
        ok = false;
    }
    return ok;
}

} // namespace omnigpu::tcp
