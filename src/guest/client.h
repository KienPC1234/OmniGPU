#pragma once

#include "common/network_utils.h"
#include <cstdint>
#include <string>

namespace omnigpu {

class Client {
public:
    explicit Client(const std::string& host, uint16_t port);
    ~Client();

    bool connect();
    void disconnect();
    bool send_data(const uint8_t* data, size_t size);
    bool receive_data(uint8_t* buffer, size_t size);

    SOCKET socket() const { return socket_; }
    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }

private:
    std::string host_;
    uint16_t port_;
    SOCKET socket_ = INVALID_SOCKET;
};

} // namespace omnigpu
