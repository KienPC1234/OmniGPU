#include "client.h"
#include <spdlog/spdlog.h>

namespace omnigpu {

Client::Client(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

Client::~Client() { disconnect(); }

bool Client::connect() {
    SPDLOG_INFO("Connecting to {}:{}...", host_, port_);

    if (!tcp::init()) {
        return false;
    }

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

    if (::connect(socket_, reinterpret_cast<sockaddr*>(&addr),
                  sizeof(addr)) != 0) {
        SPDLOG_ERROR("connect to {}:{} failed: {}", host_, port_,
                     tcp::last_error());
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
    tcp::cleanup();
    SPDLOG_INFO("Disconnected");
}

bool Client::send_data(const uint8_t* data, size_t size) {
    return tcp::send_all(socket_, data, size);
}

bool Client::receive_data(uint8_t* buffer, size_t size) {
    return tcp::recv_all(socket_, buffer, size);
}

} // namespace omnigpu
