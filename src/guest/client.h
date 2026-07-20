#pragma once

#include "common/network_utils.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace omnigpu {

class Client {
public:
    explicit Client(const std::string& host, uint16_t port);
    ~Client();

    bool connect();
    void interrupt();
    void disconnect();
    bool send_data(const uint8_t* data, size_t size);
    bool receive_data(uint8_t* buffer, size_t size);

    uint64_t sync_query(uint64_t func_id, uint64_t arg);
    void set_sync_response(uint64_t val);

    SOCKET socket() const {
        std::shared_lock lock(socket_lifetime_mutex_);
        return socket_.load(std::memory_order_acquire);
    }
    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }

    std::mutex& send_mutex() { return send_mutex_; }

private:
    std::string host_;
    uint16_t port_;
    std::atomic<SOCKET> socket_{INVALID_SOCKET};
    mutable std::shared_mutex socket_lifetime_mutex_;
    // Serializes connect/disconnect and balances platform socket init/cleanup.
    std::mutex lifecycle_mutex_;
    bool tcpInitialized_ = false;

    void disconnect_locked();

    std::mutex send_mutex_;
    std::mutex sync_mutex_;
    std::condition_variable sync_cv_;
    bool has_sync_response_ = false;
    uint64_t sync_response_val_ = 0;
};

} // namespace omnigpu
