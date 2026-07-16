#include "client.h"
#include "common/flatbuffers_utils.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <chrono>

namespace omnigpu {

Client::Client(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

Client::~Client() { disconnect(); }

bool Client::connect() {
    SPDLOG_INFO("OmniGPU Guest connecting to {}:{}...", host_, port_);

    if (!tcp::init()) return false;
    tcpInitialized_ = true;

    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        SPDLOG_ERROR("socket creation failed: {}", tcp::last_error());
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        SPDLOG_ERROR("invalid address '{}': {}", host_, tcp::last_error());
        tcp::close_socket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (::connect(socket_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        SPDLOG_ERROR("connect failed: {}", tcp::last_error());
        tcp::close_socket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    tcp::set_tcp_nodelay(socket_);
    SPDLOG_INFO("Connected to {}:{}", host_, port_);
    return true;
}

void Client::disconnect() {
    if (socket_ != INVALID_SOCKET) {
        tcp::close_socket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (tcpInitialized_) {
        tcp::cleanup();
        tcpInitialized_ = false;
    }
    SPDLOG_INFO("Disconnected");
}

bool Client::send_data(const uint8_t* data, size_t size) {
    return tcp::send_all(socket_, data, size);
}

bool Client::receive_data(uint8_t* buffer, size_t size) {
    return tcp::recv_all(socket_, buffer, size);
}

uint64_t Client::sync_query(uint64_t func_id, uint64_t arg) {
    std::lock_guard<std::recursive_mutex> lock(recv_mutex_);

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
    bool success = sync_cv_.wait_for(slock, std::chrono::seconds(5), [this]() {
        return has_sync_response_;
    });

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
