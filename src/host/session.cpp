#include "session.h"
#include "gpu_manager.h"
#include "handshake.h"
#include "common/flatbuffers_utils.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu {

Session::Session(SOCKET clientFd, GpuManager& gpuMgr, int gpuIndex)
    : clientFd_(clientFd), gpuMgr_(gpuMgr), gpuIndex_(gpuIndex) {}

Session::~Session() { stop(); }

void Session::start() {
    running_ = true;
    thread_ = std::thread(&Session::handle_client, this);
}

void Session::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool Session::recv_message(std::vector<uint8_t>& buffer) {
    uint32_t msgSize = 0;
    if (!tcp::recv_all(clientFd_, reinterpret_cast<uint8_t*>(&msgSize), sizeof(msgSize))) {
        return false;
    }

    buffer.resize(msgSize);
    return tcp::recv_all(clientFd_, buffer.data(), msgSize);
}

bool Session::send_data_message(uint64_t data_id, const uint8_t* payload, size_t payload_size) {
    auto builder = protocol::build_data(
        fbs::DataType_ImageData, data_id, payload, payload_size);

    auto span = builder.GetBufferSpan();
    uint32_t totalSize = static_cast<uint32_t>(span.size());

    if (!tcp::send_all(clientFd_, reinterpret_cast<const uint8_t*>(&totalSize),
                        sizeof(totalSize))) {
        return false;
    }

    return tcp::send_all(clientFd_, span.data(), span.size());
}

void Session::handle_client() {
    SPDLOG_INFO("Session started for client fd={} on GPU {}",
                static_cast<int>(clientFd_), gpuIndex_);

    // Handle initial handshake (capabilities request from guest)
    {
        std::vector<uint8_t> hb;
        if (!recv_message(hb)) {
            SPDLOG_ERROR("Failed to receive handshake from guest");
            gpuMgr_.release_gpu(gpuIndex_);
            tcp::close_socket(clientFd_);
            clientFd_ = INVALID_SOCKET;
            return;
        }

        auto* msg = protocol::verify_root(hb.data(), hb.size());
        if (msg && msg->payload_type() == fbs::MessagePayload_CapabilitiesRequest) {
            SPDLOG_INFO("Guest requested capabilities for GPU {}", gpuIndex_);
            handshake::handle_capabilities_request(clientFd_);
        } else {
            SPDLOG_WARN("Expected CapabilitiesRequest as first message");
        }
    }

    VkPhysicalDevice physDevice = gpuMgr_.gpu_device(gpuIndex_);
    if (!renderer_.init(physDevice, 800, 600)) {
        SPDLOG_ERROR("Failed to initialize headless renderer");
        gpuMgr_.release_gpu(gpuIndex_);
        tcp::close_socket(clientFd_);
        clientFd_ = INVALID_SOCKET;
        return;
    }

    while (running_) {
        std::vector<uint8_t> msgBuffer;
        if (!recv_message(msgBuffer)) {
            SPDLOG_INFO("Client disconnected or error receiving");
            break;
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

            SPDLOG_DEBUG("Received command: func_id={}, request_id={}",
                         static_cast<int>(cmd->func_id()), cmd->request_id());

            if (cmd->func_id() == fbs::FunctionId_vkQueueSubmit ||
                cmd->func_id() == fbs::FunctionId_vkQueuePresentKHR) {

                VkCommandBuffer vkCmd = renderer_.begin_frame();
                if (vkCmd == VK_NULL_HANDLE) {
                    SPDLOG_ERROR("begin_frame failed");
                    break;
                }

                if (!renderer_.submit_and_readback(framebufferPixels_)) {
                    SPDLOG_ERROR("submit_and_readback failed");
                    break;
                }

                auto compressed = compressor_.compress_jpeg(
                    framebufferPixels_, 800, 600, 85);

                if (!compressed.empty()) {
                    send_data_message(cmd->request_id(),
                                      compressed.data(), compressed.size());
                    SPDLOG_DEBUG("Sent compressed frame ({} bytes)", compressed.size());
                }
            }

            break;
        }
        case fbs::MessagePayload_DataMessage: {
            auto* data = msg->payload_as_DataMessage();
            if (!data) break;

            auto payload = data->payload();
            SPDLOG_DEBUG("Received data: type={}, id={}, size={}",
                         static_cast<int>(data->data_type()),
                         data->data_id(),
                         payload ? payload->size() : 0);
            break;
        }
        default:
            break;
        }
    }

    renderer_.shutdown();
    gpuMgr_.release_gpu(gpuIndex_);
    tcp::close_socket(clientFd_);
    clientFd_ = INVALID_SOCKET;
    SPDLOG_INFO("Session ended");
}

} // namespace omnigpu
