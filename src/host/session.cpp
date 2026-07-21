#include "session.h"
#include "gpu_manager.h"
#include "handshake.h"
#include "common/flatbuffers_utils.h"
#include "common/logger.h"
#if defined(OMNIGPU_USE_FFMPEG)
#include "ffmpeg_encoder.h"
#endif
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu {

Session::Session(SOCKET clientFd, GpuManager& gpuMgr,
                 const std::vector<int>& gpuIndices, int sessionId,
                 const HostConfig& hostConfig)
    : clientFd_(clientFd), gpuMgr_(gpuMgr),
      gpuIndices_(gpuIndices), sessionId_(sessionId),
      config_(hostConfig) {

    videoEncoder_ = create_best_encoder();
    if (!videoEncoder_) return;

#if defined(OMNIGPU_USE_FFMPEG)
    if (auto* ffmpeg = dynamic_cast<FFmpegEncoder*>(videoEncoder_.get())) {
        ffmpeg->set_encoder_options(config_.encoder.preset, config_.encoder.tuning, config_.encoder.gop_length);
    }
#endif

    uint32_t w = config_.video_width;
    uint32_t h = config_.video_height;
    int fps = config_.video_fps;
    int bitrate = config_.video_bitrate_kbps;
    VideoCodec codec = codec_from_string(config_.video_codec);

    if (videoEncoder_->init(codec, w, h, fps, bitrate)) {
        useVideoEncoder_ = true;
        // Convert host VideoCodec (H264=0, HEVC=1, AV1=2) to fbs::VideoCodec (H264=1, HEVC=2, AV1=3)
        active_video_codec_ = static_cast<uint8_t>(codec) + 1;
        SPDLOG_INFO("Session #{}: Hardware encoder {} initialized successfully "
                     "({}x{}, {}fps, {}kbps, codec={})",
                     sessionId_, videoEncoder_->name(),
                     w, h, fps, bitrate, config_.video_codec);
    } else {
        SPDLOG_WARN("Session #{}: Failed to initialize hardware encoder {}, "
                     "falling back to software compression",
                     sessionId_, videoEncoder_->name());
        videoEncoder_.reset();
    }
}

Session::~Session() { stop(); }

void Session::start() {
    running_ = true;
    fpsStart_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&Session::handle_client, this);
}

void Session::stop() {
    running_ = false;
    // Shutdown socket to interrupt blocking recv in handle_client
    if (clientFd_ != INVALID_SOCKET) {
        shutdown(clientFd_, SD_BOTH);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (videoEncoder_) {
        videoEncoder_->shutdown();
        videoEncoder_.reset();
    }
}

SessionSummary Session::summary() const {
    SessionSummary s;
    s.id = sessionId_;
    s.gpu_index = gpuIndices_.empty() ? -1 : gpuIndices_[0];
    s.gpu_team_size = static_cast<int>(gpuIndices_.size());
    s.fps = currentFps_.load();
    s.frames_rendered = framesRendered_;
    s.compressorStats = adaptiveCompressor_.stats();
    return s;
}

bool Session::recv_message(std::vector<uint8_t>& buffer, bool is_first) {
    SPDLOG_INFO("Session::recv_message starting, clientFd={}", (int)clientFd_);
    // Set receive timeout for first message to avoid deadlock on lost handshake
    if (is_first) {
#ifdef _WIN32
        DWORD timeout_ms = 30000;
        setsockopt(clientFd_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(clientFd_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
    }
    uint32_t msgSize = 0;
    if (!tcp::recv_all(clientFd_, reinterpret_cast<uint8_t*>(&msgSize), sizeof(msgSize))) {
        int err = WSAGetLastError();
        if (is_first && (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)) {
            SPDLOG_ERROR("Session #{}: timed out waiting for first command (handshake lost?)", sessionId_);
        }
        SPDLOG_INFO("Session::recv_message failed to read message size, errno={}, is_first={}", err, is_first);
        return false;
    }

    msgSize = ntohl(msgSize);
    uint32_t maxMsgSize = config_.max_msg_size_mb * 1024 * 1024;
    if (msgSize > maxMsgSize) {
        SPDLOG_ERROR("Session #{}: message size {} exceeds {} MB limit",
                     sessionId_, msgSize, config_.max_msg_size_mb);
        return false;
    }
    
    SPDLOG_INFO("Session::recv_message: resizing buffer to {}", msgSize);
    buffer.resize(msgSize);
    
    SPDLOG_INFO("Session::recv_message: reading payload bytes");
    bool res = tcp::recv_all(clientFd_, buffer.data(), msgSize);
    SPDLOG_INFO("Session::recv_message: payload read res={}", res);
    return res;
}

bool Session::send_data_message(uint64_t data_id, const uint8_t* payload, size_t payload_size, VkDeviceSize offset) {
    auto builder = protocol::build_data(
        fbs::DataType_StorageBuffer, data_id, payload, payload_size, offset);

    auto span = builder.GetBufferSpan();
    uint32_t totalSize = htonl(static_cast<uint32_t>(span.size()));

    if (!tcp::send_all(clientFd_, reinterpret_cast<const uint8_t*>(&totalSize),
                        sizeof(totalSize))) {
        return false;
    }

    return tcp::send_all(clientFd_, span.data(), span.size());
}

bool Session::send_video_frame(uint64_t frame_id, uint8_t codec,
                                const uint8_t* data, size_t data_size,
                                uint32_t width, uint32_t height,
                                uint64_t timestamp_ms, bool keyframe) {
    flatbuffers::FlatBufferBuilder fbb;

    auto data_vec = fbb.CreateVector(data, data_size);
    auto vf = fbs::CreateVideoFrame(
        fbb, static_cast<fbs::VideoCodec>(codec),
        frame_id, width, height, timestamp_ms, keyframe, data_vec);
    auto msg = fbs::CreateMessage(fbb, fbs::MessagePayload_VideoFrame, vf.Union());
    fbb.Finish(msg);

    auto span = fbb.GetBufferSpan();
    uint32_t net_size = htonl(static_cast<uint32_t>(span.size()));

    return tcp::send_all(clientFd_, reinterpret_cast<const uint8_t*>(&net_size),
                          sizeof(net_size)) &&
           tcp::send_all(clientFd_, span.data(), span.size());
}

void Session::handle_client() {
    std::string gpuList;
    for (size_t i = 0; i < gpuIndices_.size(); ++i) {
        if (i > 0) gpuList += ", ";
        gpuList += std::to_string(gpuIndices_[i]);
    }

    SPDLOG_INFO("Session #{} started for client fd={} on GPU(s) [{}]",
                sessionId_, static_cast<int>(clientFd_), gpuList);

    uint32_t client_pref_w = 0;
    uint32_t client_pref_h = 0;

    // Handle initial handshake + authentication
    {
        auto hs = handshake::handle_capabilities_request(clientFd_,
            config_.auth_token);
        if (!hs.ok) {
            SPDLOG_ERROR("Session #{}: Handshake failed: {}", sessionId_, hs.error_msg);
            for (int idx : gpuIndices_) gpuMgr_.release_gpu(idx);
            tcp::close_socket(clientFd_);
            clientFd_ = INVALID_SOCKET;
            return;
        }
        client_pref_w = config_.render_width;
        client_pref_h = config_.render_height;
        SPDLOG_INFO("Session #{}: Guest authenticated (v{}, compute={}, large_bufs={})",
                    sessionId_, hs.client_version, hs.compute_mode, hs.large_buffers);
    }

    uint32_t rw = config_.render_width;
    uint32_t rh = config_.render_height;
    if (client_pref_w > 0 && client_pref_h > 0) {
        uint32_t max_w = 1920;
        uint32_t max_h = 1080;
        if (client_pref_w > max_w || client_pref_h > max_h) {
            double aspect = static_cast<double>(client_pref_w) / client_pref_h;
            if (aspect > 1.0) {
                client_pref_w = max_w;
                client_pref_h = static_cast<uint32_t>(max_w / aspect);
            } else {
                client_pref_h = max_h;
                client_pref_w = static_cast<uint32_t>(max_h * aspect);
            }
        }
        rw = client_pref_w;
        rh = client_pref_h;
        SPDLOG_INFO("Session #{}: Using dynamic resolution from client: {}x{}", sessionId_, rw, rh);
    }

    // Initialize compute engine for compute workloads
    if (!computeEngine_.init(gpuMgr_, gpuIndices_)) {
        SPDLOG_ERROR("Session #{}: Failed to initialize multi-GPU compute engine", sessionId_);
        for (int idx : gpuIndices_) gpuMgr_.release_gpu(idx);
        tcp::close_socket(clientFd_);
        clientFd_ = INVALID_SOCKET;
        return;
    }

    // Initialize buffer manager for persistent GPU-side buffers
    {
        const auto& primary = computeEngine_.primary();
        bufferMgr_.set_device(primary.device, primary.queue,
                              primary.queueFamily, primary.cmdPool);
        bufferMgr_.set_physical_device(primary.physDevice);
        SPDLOG_INFO("Session #{}: Buffer manager initialized (VRAM budget: {} MB)",
                    sessionId_, primary.dedicatedMemory / (1024 * 1024));
    }

    // Initialize command dispatcher with the host Vulkan device
    const auto& primary = computeEngine_.primary();
    commandDispatcher_.set_device(
        primary.physDevice,
        primary.device,
        primary.queue,
        primary.queueFamily,
        primary.cmdPool);
    commandDispatcher_.set_framebuffer_size(rw, rh);
    commandDispatcher_.setup_framebuffer();
    commandDispatcher_.set_vram_budget(config_.per_session_memory_budget);

    // Wire up readback callback: when guest invalidates memory, send data back
    commandDispatcher_.set_send_data_callback(
        [this](uint64_t buffer_id, const uint8_t* data, size_t size, VkDeviceSize offset) -> bool {
            return send_data_message(buffer_id, data, size, offset);
        });

    SPDLOG_INFO("Session #{} entering main loop", sessionId_);
    bool firstMsg = true;
    try {
    while (running_) {
        std::vector<uint8_t> msgBuffer;
        SPDLOG_INFO("Session #{}: calling recv_message (loop iter, running={})", sessionId_, running_.load());
        if (!recv_message(msgBuffer, firstMsg)) {
            break;
        }
        firstMsg = false;
        SPDLOG_INFO("Session #{}: message received, size={}", sessionId_, msgBuffer.size());

        // Check for synchronous query (any size >= 16, func_id in range 0x80-0x8F)
        if (msgBuffer.size() >= 16) {
            uint64_t query_type = 0;
            std::memcpy(&query_type, msgBuffer.data(), 8);
            if (query_type >= 0x80 && query_type <= 0x8F) {
            uint64_t query_arg = 0;
            std::memcpy(&query_arg, msgBuffer.data() + 8, 8);

            auto respond_raw = [&](const uint8_t* data, size_t size) {
                auto builder = protocol::build_data(
                    fbs::DataType_Unknown, 0, data, size);
                auto span = builder.GetBufferSpan();
                uint32_t totalSize = htonl(static_cast<uint32_t>(span.size()));

                if (!tcp::send_all(clientFd_, reinterpret_cast<const uint8_t*>(&totalSize), sizeof(totalSize)) ||
                    !tcp::send_all(clientFd_, span.data(), span.size())) {
                    SPDLOG_WARN("Session: respond send failed for query type {}", query_type);
                }
            };

            auto respond = [&](uint64_t val) {
                uint8_t resp[8];
                std::memcpy(resp, &val, 8);
                respond_raw(resp, sizeof(resp));
            };

            auto respond_mem_req = [&](const VkMemoryRequirements& mr) {
                struct {
                    uint64_t size;
                    uint64_t alignment;
                    uint32_t memoryTypeBits;
                    uint32_t padding;
                } p;
                p.size = mr.size;
                p.alignment = mr.alignment;
                p.memoryTypeBits = mr.memoryTypeBits;
                p.padding = 0;
                respond_raw(reinterpret_cast<const uint8_t*>(&p), sizeof(p));
            };

            switch (query_type) {
            case 0x80: { // DEVICE_ADDRESS_QUERY
                VkBuffer hostBuf = commandDispatcher_.mapper().get_buffer(query_arg);
                if (hostBuf) {
                    VkBufferDeviceAddressInfo bdai{};
                    bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                    bdai.buffer = hostBuf;
                    respond(vkGetBufferDeviceAddress(
                        commandDispatcher_.mapper().device(), &bdai));
                } else { respond(0); }
                continue;
            }
            case 0x81: { // BUFFER_OPAQUE_CAPTURE_ADDRESS_QUERY
                VkBuffer hostBuf = commandDispatcher_.mapper().get_buffer(query_arg);
                if (hostBuf) {
                    VkBufferDeviceAddressInfo bdai{};
                    bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                    bdai.buffer = hostBuf;
                    respond(vkGetBufferOpaqueCaptureAddress(
                        commandDispatcher_.mapper().device(), &bdai));
                } else { respond(0); }
                continue;
            }
            case 0x82: { // MEMORY_OPAQUE_CAPTURE_ADDRESS_QUERY
                VkDeviceMemory hostMem = commandDispatcher_.mapper().get_device_memory(query_arg);
                if (hostMem) {
                    VkDeviceMemoryOpaqueCaptureAddressInfo mai{};
                    mai.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO;
                    mai.memory = hostMem;
                    respond(vkGetDeviceMemoryOpaqueCaptureAddress(
                        commandDispatcher_.mapper().device(), &mai));
                } else { respond(0); }
                continue;
            }
            case 0x83: { // BUFFER_MEMORY_REQUIREMENTS_QUERY
                VkBuffer hostBuf = commandDispatcher_.mapper().get_buffer(query_arg);
                if (hostBuf) {
                    VkMemoryRequirements mr{};
                    vkGetBufferMemoryRequirements(
                        commandDispatcher_.mapper().device(), hostBuf, &mr);
                    respond_mem_req(mr);
                } else {
                    VkMemoryRequirements empty{};
                    respond_mem_req(empty);
                }
                continue;
            }
            case 0x84: { // IMAGE_MEMORY_REQUIREMENTS_QUERY
                VkImage hostImg = commandDispatcher_.mapper().get_image(query_arg);
                if (hostImg) {
                    VkMemoryRequirements mr{};
                    vkGetImageMemoryRequirements(
                        commandDispatcher_.mapper().device(), hostImg, &mr);
                    respond_mem_req(mr);
                } else {
                    VkMemoryRequirements empty{};
                    respond_mem_req(empty);
                }
                continue;
            }
            case 0x85: { // WAIT_FOR_FENCE_QUERY
                VkFence fence = commandDispatcher_.mapper().get_fence(query_arg);
                if (fence) {
                    VkResult res = vkWaitForFences(
                        commandDispatcher_.mapper().device(), 1, &fence, VK_TRUE, 5000000000ULL);
                    respond(static_cast<uint64_t>(res));
                } else { respond(static_cast<uint64_t>(VK_SUCCESS)); }
                continue;
            }
            case 0x87: { // GET_FENCE_STATUS_QUERY
                VkFence fence = commandDispatcher_.mapper().get_fence(query_arg);
                if (fence) {
                    VkResult res = vkGetFenceStatus(
                        commandDispatcher_.mapper().device(), fence);
                    respond(static_cast<uint64_t>(res));
                } else { respond(static_cast<uint64_t>(VK_SUCCESS)); }
                continue;
            }
            case 0x88: { // DEVICE_WAIT_IDLE_QUERY
                VkResult res = vkDeviceWaitIdle(commandDispatcher_.mapper().device());
                respond(static_cast<uint64_t>(res));
                continue;
            }
            case 0x89: { // QUEUE_WAIT_IDLE_QUERY
                VkQueue queue = commandDispatcher_.mapper().get_queue(query_arg);
                if (queue) {
                    VkResult res = vkQueueWaitIdle(queue);
                    respond(static_cast<uint64_t>(res));
                } else { respond(static_cast<uint64_t>(VK_SUCCESS)); }
                continue;
            }
            case 0x8a: { // GET_SEMAPHORE_COUNTER_VALUE_QUERY
                VkSemaphore semaphore = commandDispatcher_.mapper().get_semaphore(query_arg);
                if (semaphore) {
                    uint64_t val = 0;
                    auto pfnGetSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
                        vkGetDeviceProcAddr(commandDispatcher_.mapper().device(), "vkGetSemaphoreCounterValue"));
                    if (pfnGetSemaphoreCounterValue) {
                        pfnGetSemaphoreCounterValue(commandDispatcher_.mapper().device(), semaphore, &val);
                    }
                    respond(val);
                } else { respond(0); }
                continue;
            }
            case 0x8b: { // GET_QUERY_POOL_RESULTS_QUERY
                respond(static_cast<uint64_t>(VK_SUCCESS));
                continue;
            }
            case 0x8c: { // GET_IMAGE_SUBRESOURCE_LAYOUT_QUERY
                respond(static_cast<uint64_t>(VK_SUCCESS));
                continue;
            }
            case 0x8d: { // INVALIDATE_MEMORY_RANGES_QUERY
                respond(static_cast<uint64_t>(VK_SUCCESS));
                continue;
            }
            case 0x8e: { // DEVICE_BUFFER_MEMORY_REQUIREMENTS_QUERY
                // query_arg contains the size of serialized VkBufferCreateInfo
                // The actual create info data follows the 16-byte header
                // We need to read the extended message
                // For now, handle as simple: query_arg = serialized size, no extra data
                // This is a placeholder — actual implementation reads from larger message
                VkMemoryRequirements mr{};
                mr.size = query_arg;
                mr.alignment = 256;
                mr.memoryTypeBits = 0x1F;
                respond_mem_req(mr);
                continue;
            }
            default:
                continue;
            }
            } // if (query_type >= 0x80)
        }

        auto* msg = protocol::verify_root(msgBuffer.data(), msgBuffer.size());
        if (!msg) {
            SPDLOG_ERROR("Invalid FlatBuffers message from client");
            continue;
        }

        switch (msg->payload_type()) {
        case fbs::MessagePayload_CommandMessage: {
            auto* cmd = msg->payload_as_CommandMessage();
            if (!cmd) break;

            auto func_id = cmd->func_id();
            auto* args = cmd->args();
            size_t args_size = args ? args->size() : 0;

            SPDLOG_INFO("Session #{}: {} (request_id={})",
                         sessionId_,
                         fbs::EnumNameFunctionId(func_id), cmd->request_id());

            // Dispatch to command replay engine for ALL commands
            if (args && args_size > 0) {
                commandDispatcher_.dispatch(func_id, args->data(), args_size);
            }

            // On vkQueueSubmit / vkQueueSubmit2 / vkQueuePresentKHR: flush and readback
            if (func_id == fbs::FunctionId_vkQueueSubmit ||
                func_id == fbs::FunctionId_vkQueueSubmit2 ||
                func_id == fbs::FunctionId_vkQueuePresentKHR) {

                std::vector<uint8_t> pixels;
                SPDLOG_INFO("Session #{}: calling flush_and_readback", sessionId_);
                spdlog::default_logger()->flush();
                if (!commandDispatcher_.flush_and_readback(pixels)) {
                    SPDLOG_ERROR("flush_and_readback failed");
                    break;
                }
                SPDLOG_INFO("Session #{}: flush_and_readback OK, pixels={} bytes", sessionId_, pixels.size());
                framebufferPixels_ = std::move(pixels);

                if (useVideoEncoder_) {
                    SPDLOG_INFO("Session #{}: encoding with video encoder", sessionId_);
                    std::vector<EncodedPacket> packets;
                    if (videoEncoder_->encode(framebufferPixels_, packets)) {
                        SPDLOG_INFO("Session #{}: encoded {} packets", sessionId_, packets.size());
                        auto sendStart = std::chrono::steady_clock::now();
                        for (const auto& packet : packets) {
                            send_video_frame(
                                cmd->request_id(),
                                active_video_codec_,
                                packet.data.data(), packet.data.size(),
                                rw, rh,
                                packet.pts,
                                packet.isKeyframe);
                        }
                        auto sendEnd = std::chrono::steady_clock::now();
                        double sendMs = std::chrono::duration<double, std::milli>(
                            sendEnd - sendStart).count();
                        if (!packets.empty()) {
                            adaptiveCompressor_.record_send(packets[0].data.size(), sendMs);
                        }
                    }
                } else {
                    auto compressed = adaptiveCompressor_.compress(
                        framebufferPixels_, rw, rh);

                    if (!compressed.empty()) {
                        auto sendStart = std::chrono::steady_clock::now();

                        send_video_frame(cmd->request_id(), 0,
                                         compressed.data(), compressed.size(),
                                         rw, rh,
                                         std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch()).count(),
                                         true);

                        auto sendEnd = std::chrono::steady_clock::now();
                        double sendMs = std::chrono::duration<double, std::milli>(
                            sendEnd - sendStart).count();

                        adaptiveCompressor_.record_send(compressed.size(), sendMs);
                    }
                }

                framesRendered_++;
                SPDLOG_INFO("Session #{}: frame done (total={})", sessionId_, framesRendered_);
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration<double>(now - fpsStart_).count();
                if (elapsed >= 1.0) {
                    currentFps_.store(static_cast<double>(framesRendered_) / elapsed);
                    fpsStart_ = now;
                    framesRendered_ = 0;
                }
            }

            break;
        }
        case fbs::MessagePayload_DataMessage: {
            auto* data = msg->payload_as_DataMessage();
            if (!data) break;

            SPDLOG_DEBUG("Received data: type={}, id={}, size={}",
                         static_cast<int>(data->data_type()),
                         data->data_id(),
                         data->payload() ? data->payload()->size() : 0);
            break;
        }
        case fbs::MessagePayload_ResourceCacheUpload:
        case fbs::MessagePayload_ResourceCacheEvictRequest:
            // Resource cache is not used by any guest hook (dead code path)
            break;
        default:
            break;
        }
        SPDLOG_INFO("Session #{}: loop iter end (post-switch)", sessionId_);
        spdlog::default_logger()->flush();
    }

    SPDLOG_INFO("Session #{} cleanup starting (normal exit)", sessionId_);
    spdlog::default_logger()->flush();
    commandDispatcher_.cleanup();
    SPDLOG_INFO("Session #{} cleanup: dispatcher done", sessionId_);
    spdlog::default_logger()->flush();
    bufferMgr_.cleanup();
    SPDLOG_INFO("Session #{} cleanup: bufferMgr done", sessionId_);
    spdlog::default_logger()->flush();
    computeEngine_.shutdown();
    SPDLOG_INFO("Session #{} cleanup: computeEngine done", sessionId_);
    spdlog::default_logger()->flush();
    } catch (const std::exception& e) {
        SPDLOG_CRITICAL("Session #{} crashed with exception: {}", sessionId_, e.what());
        spdlog::default_logger()->flush();
    } catch (...) {
        SPDLOG_CRITICAL("Session #{} crashed with unknown exception", sessionId_);
        spdlog::default_logger()->flush();
    }
    tcp::close_socket(clientFd_);
    clientFd_ = INVALID_SOCKET;
    running_ = false;
    SPDLOG_INFO("Session #{} ended", sessionId_);
}

} // namespace omnigpu
