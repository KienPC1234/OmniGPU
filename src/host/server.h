#pragma once

#include "common/network_utils.h"
#include "config.h"
#include "gpu_manager.h"
#include "session.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace omnigpu {

class Session;

class Server {
public:
    explicit Server(const HostConfig& config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    bool start();
    void run();
    void stop();

    // CLI queries
    int gpu_count() const;
    GpuInfo gpu_info(int index) const;
    std::vector<SessionSummary> session_summaries() const;
    bool disconnect_session(int sessionId);
    const HostConfig& config() const { return config_; }

private:
    HostConfig config_;
    uint16_t port_;
    int nextSessionId_ = 1;
    SOCKET listenFd_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    GpuManager gpuMgr_;

    mutable std::mutex sessionsMutex_;
    std::vector<std::unique_ptr<Session>> sessions_;

    // Rate limiting
    mutable std::mutex rateMutex_;
    std::deque<std::chrono::steady_clock::time_point> connTimestamps_;

    void cleanup_stopped_sessions();
};

} // namespace omnigpu
