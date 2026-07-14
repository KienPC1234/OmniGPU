#pragma once

#include "common/network_utils.h"
#include "compressor.h"
#include "renderer.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace omnigpu {

class GpuManager;

class Session {
public:
    Session(SOCKET clientFd, GpuManager& gpuMgr, int gpuIndex);
    ~Session();

    void start();
    void stop();
    bool is_running() const { return running_; }

private:
    SOCKET clientFd_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    GpuManager& gpuMgr_;
    int gpuIndex_ = -1;
    Renderer renderer_;
    Compressor compressor_;
    std::vector<uint8_t> framebufferPixels_;

    void handle_client();
    bool recv_message(std::vector<uint8_t>& buffer);
    bool send_data_message(uint64_t data_id, const uint8_t* payload, size_t payload_size);
};

} // namespace omnigpu
