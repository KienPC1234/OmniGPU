#pragma once

#include "common/network_utils.h"
#include "adaptive_compressor.h"
#include "buffer_manager.h"
#include "command_dispatcher.h"
#include "config.h"
#include "multi_gpu_compute.h"
#include "video_encoder.h"
#include <atomic>
#include <chrono>
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
    double fps = 0.0;
    uint64_t frames_rendered = 0;
    AdaptiveCompressor::Stats compressorStats{};
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
    GpuManager& gpuMgr_;
    std::vector<int> gpuIndices_;
    int sessionId_ = 0;
    MultiGpuCompute computeEngine_;
    host::BufferManager bufferMgr_;
    host::CommandDispatcher commandDispatcher_;
    AdaptiveCompressor adaptiveCompressor_;
    std::vector<uint8_t> framebufferPixels_;
    std::unique_ptr<VideoEncoder> videoEncoder_;
    bool useVideoEncoder_ = false;
    HostConfig config_;
    uint8_t active_video_codec_ = 1; // fbs::VideoCodec H.264 default

    // FPS tracking
    std::chrono::steady_clock::time_point fpsStart_;
    uint64_t framesRendered_ = 0;
    std::atomic<double> currentFps_{0.0};

    void handle_client();
    bool recv_message(std::vector<uint8_t>& buffer, bool is_first = false);
    bool send_data_message(uint64_t data_id, const uint8_t* payload, size_t payload_size, VkDeviceSize offset = 0);
    bool send_video_frame(uint64_t frame_id, uint8_t codec,
                          const uint8_t* data, size_t data_size,
                          uint32_t width, uint32_t height,
                          uint64_t timestamp_ms, bool keyframe);
};

} // namespace omnigpu
