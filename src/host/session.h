#pragma once

#include "common/network_utils.h"
#include "buffer_manager.h"
#include "command_dispatcher.h"
#include "config.h"
#include "multi_gpu_compute.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace omnigpu {

class GpuManager;

struct SessionSummary {
    int id = 0;
    int gpu_index = -1;
    int gpu_team_size = 1;
};

class Session {
public:
    Session(SOCKET clientFd, GpuManager& gpuMgr,
            const std::vector<int>& gpuIndices, int sessionId,
            const HostConfig& hostConfig);
    ~Session();

    void start();
    void stop();
    bool is_running() const { return running_; }

    SessionSummary summary() const;
    int gpu_index() const { return gpuIndices_.empty() ? -1 : gpuIndices_[0]; }

private:
    SOCKET clientFd_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    bool isComputeMode_ = false;
    GpuManager& gpuMgr_;
    std::vector<int> gpuIndices_;
    int sessionId_ = 0;
    MultiGpuCompute computeEngine_;
    host::BufferManager bufferMgr_;
    host::CommandDispatcher commandDispatcher_;
    HostConfig config_;

    void handle_client();
    bool recv_message(std::vector<uint8_t>& buffer, bool is_first = false);
    bool send_data_message(uint64_t data_id, const uint8_t* payload, size_t payload_size, VkDeviceSize offset = 0);
    void readback_all_buffers() { commandDispatcher_.readback_all_buffers(); }
};

} // namespace omnigpu
