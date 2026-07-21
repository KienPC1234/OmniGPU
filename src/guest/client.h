#pragma once

#include "common/network_utils.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <condition_variable>

namespace omnigpu {

class Client {
public:
    explicit Client(const std::string& host, uint16_t port);
    ~Client();

    bool connect();
    void disconnect();
    bool send_data(const uint8_t* data, size_t size);
    bool receive_data(uint8_t* buffer, size_t size);

    uint64_t sync_query(uint64_t func_id, uint64_t arg);
    uint64_t sync_query_ext(uint64_t func_id, const uint8_t* extra, size_t extra_size);
    void set_sync_response(uint64_t val);

    SOCKET socket() const { return socket_; }
    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }

    std::mutex& send_mutex() { return send_mutex_; }

private:
    std::string host_;
    uint16_t port_;
    SOCKET socket_ = INVALID_SOCKET;
    bool tcpInitialized_ = false;

    std::mutex send_mutex_;
    std::mutex sync_mutex_;
    std::condition_variable sync_cv_;
    bool has_sync_response_ = false;
    uint64_t sync_response_val_ = 0;
    std::atomic<bool> sync_in_flight_{false};
};

} // namespace omnigpu
