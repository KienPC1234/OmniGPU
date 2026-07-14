#include "cache_manager.h"
#include "client.h"
#include "command_batch.h"
#include "loader.h"
#include "vk_intercept.h"
#include "common/flatbuffers_utils.h"
#include "common/gpu_caps.h"
#include "common/logger.h"
#include "omnigpu_protocol_generated.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <cstring>

namespace {

bool do_handshake(omnigpu::Client& client) {
    using namespace omnigpu;

    // Step 1: Try loading cached capabilities
    caps::GpuCapabilities cached_caps;
    cache::CacheManager cache(client.host(), client.port());

    if (cache.load(cached_caps)) {
        SPDLOG_INFO("Using cached GPU capabilities: {}", cached_caps.gpu_name);
        SPDLOG_INFO("  Max memory allocation: {}",
                    cached_caps.max_memory_allocation);
        SPDLOG_INFO("  Max push constants: {}",
                    cached_caps.max_push_constants_size);
        SPDLOG_INFO("  Max image dimension: {}",
                    cached_caps.max_image_dimension_2d);
        return true;
    }

    // Step 2: No cache — send handshake request to host
    SPDLOG_INFO("No cached GPU caps found, requesting from host...");

    flatbuffers::FlatBufferBuilder builder;
    auto req = fbs::CreateCapabilitiesRequest(builder, 1);
    auto msg = fbs::CreateMessage(
        builder, fbs::MessagePayload_CapabilitiesRequest, req.Union());
    builder.Finish(msg);

    auto span = builder.GetBufferSpan();
    uint32_t net_size = htonl(static_cast<uint32_t>(span.size()));

    if (!client.send_data(reinterpret_cast<const uint8_t*>(&net_size),
                          sizeof(net_size))) {
        SPDLOG_ERROR("Failed to send handshake request");
        return false;
    }
    if (!client.send_data(span.data(), span.size())) {
        SPDLOG_ERROR("Failed to send handshake payload");
        return false;
    }

    // Step 3: Receive capabilities response
    uint32_t resp_size = 0;
    if (!client.receive_data(reinterpret_cast<uint8_t*>(&resp_size),
                              sizeof(resp_size))) {
        SPDLOG_ERROR("Failed to receive handshake response size");
        return false;
    }

    std::vector<uint8_t> resp_buf(resp_size);
    if (!client.receive_data(resp_buf.data(), resp_size)) {
        SPDLOG_ERROR("Failed to receive handshake response");
        return false;
    }

    // Step 4: Parse and cache
    auto* root = protocol::verify_root(resp_buf.data(), resp_buf.size());
    if (!root || root->payload_type() != fbs::MessagePayload_CapabilitiesResponse) {
        SPDLOG_ERROR("Invalid handshake response");
        return false;
    }

    auto* caps = root->payload_as_CapabilitiesResponse();
    if (!caps || !caps->gpu_name()) {
        SPDLOG_ERROR("Invalid capabilities in response");
        return false;
    }

    caps::GpuCapabilities gpu_caps;
    gpu_caps.gpu_name = caps->gpu_name()->str();
    gpu_caps.driver_version = caps->driver_version();
    gpu_caps.api_version = caps->api_version();
    gpu_caps.max_memory_allocation = caps->max_memory_allocation();
    gpu_caps.max_push_constants_size = caps->max_push_constants_size();
    gpu_caps.max_bound_descriptor_sets = caps->max_bound_descriptor_sets();
    gpu_caps.max_per_stage_resources = caps->max_per_stage_resources();
    gpu_caps.max_image_dimension_2d = caps->max_image_dimension_2d();
    gpu_caps.timestamp = caps->timestamp();

    cache.save(gpu_caps);

    SPDLOG_INFO("Host GPU capabilities received and cached: {}",
                gpu_caps.gpu_name);
    SPDLOG_INFO("  API version: {}.{}.{}",
                VK_API_VERSION_MAJOR(gpu_caps.api_version),
                VK_API_VERSION_MINOR(gpu_caps.api_version),
                VK_API_VERSION_PATCH(gpu_caps.api_version));
    SPDLOG_INFO("  Max push constants: {} bytes",
                gpu_caps.max_push_constants_size);
    SPDLOG_INFO("  Max image dimension: {}",
                gpu_caps.max_image_dimension_2d);

    return true;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    omnigpu::init_logger();

    std::string host = "127.0.0.1";
    uint16_t port = 9443;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<uint16_t>(std::atoi(argv[2]));

    SPDLOG_INFO("OmniGPU Guest starting, host={}:{}", host, port);

    // Step 1: Initialize translation layers (Zink, clvk, Vulkan ICD)
    omnigpu::loader::initialize();

    // Step 2: Connect to OmniGPU Host
    omnigpu::Client client(host, port);
    if (!client.connect()) {
        SPDLOG_ERROR("Failed to connect to host");
        return 1;
    }

    // Step 3: Handshake — query and cache host GPU capabilities
    if (!do_handshake(client)) {
        SPDLOG_WARN("Handshake failed, continuing with defaults");
    }

    // Step 4: Create command batch and register with intercept layer
    omnigpu::batch::CommandBatch batch(client.socket());
    omnigpu::intercept::set_batch(&batch);

    // Step 5: Initialize Vulkan intercept hooks (after batch is set)
    omnigpu::intercept::initialize_hooks();

    SPDLOG_INFO("Guest connected. All APIs unified through Vulkan pipeline:");
    SPDLOG_INFO("  OpenGL  → {} → Vulkan → omniGPU → Host GPU",
                omnigpu::loader::get_layers().zink_available ? "Zink" : "(not available)");
    SPDLOG_INFO("  OpenCL  → {} → Vulkan → omniGPU → Host GPU",
                omnigpu::loader::get_layers().clvk_available ? "clvk" : "(not available)");
    SPDLOG_INFO("  Vulkan  → omniGPU → Host GPU");

    // Wait for shutdown
    std::this_thread::sleep_for(std::chrono::hours(24));

    batch.force_flush();
    omnigpu::intercept::shutdown_hooks();
    omnigpu::loader::shutdown();
    return 0;
}
