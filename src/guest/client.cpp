#include "client.h"
#include "common/flatbuffers_utils.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <spdlog/spdlog.h>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#endif

namespace omnigpu {
namespace {

constexpr auto kConnectTimeout = std::chrono::seconds(5);

#ifdef _WIN32
bool set_nonblocking(SOCKET socket, bool enabled) {
    u_long mode = enabled ? 1UL : 0UL;
    return ::ioctlsocket(socket, FIONBIO, &mode) == 0;
}
#else
bool set_nonblocking(SOCKET socket, bool enabled) {
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags < 0) return false;
    const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(socket, F_SETFL, updated) == 0;
}
#endif

bool connect_until(SOCKET socket, const sockaddr* address,
#ifdef _WIN32
                   int address_length,
#else
                   socklen_t address_length,
#endif
                   std::chrono::steady_clock::time_point deadline) {
    if (!set_nonblocking(socket, true)) return false;

    const int initial = ::connect(socket, address, address_length);
    if (initial == 0) return set_nonblocking(socket, false);

#ifdef _WIN32
    const int connect_error = WSAGetLastError();
    if (connect_error != WSAEWOULDBLOCK && connect_error != WSAEINPROGRESS &&
        connect_error != WSAEALREADY) {
        (void)set_nonblocking(socket, false);
        return false;
    }
#else
    if (errno != EINPROGRESS && errno != EALREADY) {
        (void)set_nonblocking(socket, false);
        return false;
    }
#endif

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        const auto milliseconds = std::max<int64_t>(
            1, std::chrono::duration_cast<std::chrono::milliseconds>(remaining)
                   .count());
#ifdef _WIN32
        fd_set writable;
        fd_set failures;
        FD_ZERO(&writable);
        FD_ZERO(&failures);
        FD_SET(socket, &writable);
        FD_SET(socket, &failures);
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(milliseconds / 1000);
        timeout.tv_usec = static_cast<long>((milliseconds % 1000) * 1000);
        const int ready = ::select(0, nullptr, &writable, &failures, &timeout);
        if (ready == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
#else
        pollfd descriptor{};
        descriptor.fd = socket;
        descriptor.events = POLLOUT;
        const int poll_timeout = milliseconds > std::numeric_limits<int>::max()
            ? std::numeric_limits<int>::max()
            : static_cast<int>(milliseconds);
        const int ready = ::poll(&descriptor, 1, poll_timeout);
        if (ready < 0 && errno == EINTR) continue;
#endif
        if (ready <= 0) break;

        int socket_error = 0;
#ifdef _WIN32
        int error_length = sizeof(socket_error);
        const int option_result = ::getsockopt(
            socket, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&socket_error), &error_length);
#else
        socklen_t error_length = sizeof(socket_error);
        const int option_result = ::getsockopt(
            socket, SOL_SOCKET, SO_ERROR, &socket_error, &error_length);
#endif
        const bool connected = option_result == 0 && socket_error == 0;
        if (!set_nonblocking(socket, false)) return false;
        return connected;
    }

    (void)set_nonblocking(socket, false);
    return false;
}

} // namespace

Client::Client(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

Client::~Client() { disconnect(); }

bool Client::connect() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    SPDLOG_INFO("OmniGPU Guest connecting to {}:{}...", host_, port_);

    // Make reconnect deterministic and balance a prior WSAStartup on Windows.
    disconnect_locked();
    if (!tcp::init()) return false;
    tcpInitialized_ = true;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string port_string = std::to_string(port_);
    addrinfo* addresses = nullptr;
    const int lookup_result = ::getaddrinfo(
        host_.c_str(), port_string.c_str(), &hints, &addresses);
    if (lookup_result != 0) {
#ifdef _WIN32
        const char* message = gai_strerrorA(lookup_result);
#else
        const char* message = gai_strerror(lookup_result);
#endif
        SPDLOG_ERROR("getaddrinfo failed for '{}': {}", host_,
                     message != nullptr ? message : "unknown resolver error");
        (void)message;
        disconnect_locked();
        return false;
    }

    SOCKET connected_socket = INVALID_SOCKET;
    const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    for (addrinfo* address = addresses; address != nullptr;
         address = address->ai_next) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        SOCKET candidate = ::socket(address->ai_family, address->ai_socktype,
                                    address->ai_protocol);
        if (candidate == INVALID_SOCKET) continue;

#ifdef _WIN32
        const int address_length = static_cast<int>(address->ai_addrlen);
#else
        const socklen_t address_length = address->ai_addrlen;
#endif
        if (connect_until(candidate, address->ai_addr, address_length,
                          deadline)) {
            connected_socket = candidate;
            break;
        }
        tcp::close_socket(candidate);
    }
    ::freeaddrinfo(addresses);

    if (connected_socket == INVALID_SOCKET) {
        SPDLOG_ERROR("connect to {}:{} failed: {}", host_, port_,
                     tcp::last_error());
        disconnect_locked();
        return false;
    }

    {
        std::unique_lock lock(socket_lifetime_mutex_);
        socket_.store(connected_socket, std::memory_order_release);
    }
    if (!tcp::set_tcp_nodelay(connected_socket)) {
        SPDLOG_WARN("Connected, but TCP_NODELAY could not be enabled");
    }
    SPDLOG_INFO("Connected to {}:{}", host_, port_);
    return true;
}

void Client::interrupt() {
    std::shared_lock lock(socket_lifetime_mutex_);
    const SOCKET fd = socket_.load(std::memory_order_acquire);
    if (fd != INVALID_SOCKET) tcp::shutdown_socket(fd);
}

void Client::disconnect() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    disconnect_locked();
}

void Client::disconnect_locked() {
    // Wake any blocking sender/receiver first. The exclusive lifetime lock then
    // waits for those operations to leave the kernel before closing the fd, so
    // another thread cannot accidentally use a recycled descriptor.
    interrupt();
    std::unique_lock lock(socket_lifetime_mutex_);
    const SOCKET fd = socket_.exchange(INVALID_SOCKET, std::memory_order_acq_rel);
    if (fd != INVALID_SOCKET) {
        tcp::close_socket(fd);
    }
    if (tcpInitialized_) {
        tcp::cleanup();
        tcpInitialized_ = false;
    }
    lock.unlock();

    // Do not make a synchronous query wait for its full timeout after an
    // intentional disconnect.
    {
        std::lock_guard<std::mutex> sync_lock(sync_mutex_);
        sync_response_val_ = 0;
        has_sync_response_ = true;
    }
    sync_cv_.notify_all();
    SPDLOG_DEBUG("Disconnected");
}

bool Client::send_data(const uint8_t* data, size_t size) {
    std::shared_lock lock(socket_lifetime_mutex_);
    const SOCKET fd = socket_.load(std::memory_order_acquire);
    return fd != INVALID_SOCKET && tcp::send_all(fd, data, size);
}

bool Client::receive_data(uint8_t* buffer, size_t size) {
    std::shared_lock lock(socket_lifetime_mutex_);
    const SOCKET fd = socket_.load(std::memory_order_acquire);
    return fd != INVALID_SOCKET && tcp::recv_all(fd, buffer, size);
}

uint64_t Client::sync_query(uint64_t func_id, uint64_t arg) {
    std::lock_guard<std::mutex> send_lock(send_mutex_);

    {
        std::lock_guard<std::mutex> slock(sync_mutex_);
        has_sync_response_ = false;
        sync_response_val_ = 0;
    }

    uint32_t net_sz = htonl(16);
    uint8_t req[20];
    std::memcpy(req, &net_sz, 4);
    std::memcpy(req + 4, &func_id, 8);
    std::memcpy(req + 12, &arg, 8);
    if (!send_data(req, sizeof(req))) {
        SPDLOG_ERROR("sync_query: send failed");
        return 0;
    }

    std::unique_lock<std::mutex> slock(sync_mutex_);
    const bool success = sync_cv_.wait_for(
        slock, std::chrono::seconds(5), [this]() { return has_sync_response_; });

    if (!success) {
        SPDLOG_ERROR("sync_query: timed out waiting for response");
        return 0;
    }

    return sync_response_val_;
}

void Client::set_sync_response(uint64_t val) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    sync_response_val_ = val;
    has_sync_response_ = true;
    sync_cv_.notify_all();
}

} // namespace omnigpu
