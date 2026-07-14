#pragma once

#include "common/network_utils.h"
#include "gpu_manager.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace omnigpu {

class Session;

class Server {
public:
    explicit Server(uint16_t port);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    bool start();
    void run();
    void stop();

private:
    uint16_t port_;
    SOCKET listenFd_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    GpuManager gpuMgr_;

    std::mutex sessionsMutex_;
    std::vector<std::unique_ptr<Session>> sessions_;

    void cleanup_stopped_sessions();
};

} // namespace omnigpu
