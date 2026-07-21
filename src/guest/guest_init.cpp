#include "guest_init.h"
#include "cache_manager.h"
#include "client.h"
#include "command_batch.h"
#include "guest_config.h"
#include "icd_registry.h"
#include "video_decoder.h"
#include "vk_intercept.h"
#include "common/flatbuffers_utils.h"
#include "common/gpu_caps.h"
#include "common/gpu_caps_store.h"
#include "common/logger.h"
#include "omnigpu_protocol_generated.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <mutex>

namespace omnigpu::init {

namespace {

Client* g_client = nullptr;
batch::CommandBatch* g_batch = nullptr;
video::VideoDecoder* g_decoder = nullptr;
std::thread* g_recv_thread = nullptr;
std::atomic<bool> g_running{false};
std::once_flag g_init_flag;
bool g_init_result = false;
std::mutex g_connect_mutex;
std::string g_host_hint;
uint16_t g_port_hint = 0;
constexpr uint32_t kHandshakeTimeoutMs = 10'000;
constexpr uint32_t kMaxHandshakeBytes = 4 * 1024 * 1024;

uint32_t remaining_handshake_ms(
    std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return 0;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now).count();
    return static_cast<uint32_t>(remaining > 0 ? remaining : 1);
}

bool do_handshake(Client& client, const std::string& auth_token) {
    const auto handshake_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(kHandshakeTimeoutMs);
    auto receive_handshake = [&](uint8_t* buffer, size_t size) {
        const uint32_t remaining = remaining_handshake_ms(handshake_deadline);
        return remaining != 0 &&
               client.receive_data_for(buffer, size, remaining);
    };

    caps::GpuCapabilities cached_caps;
    cache::CacheManager cache(client.host(), client.port());
    bool is_cached = cache.load(cached_caps);

    if (is_cached) {
        caps::store(cached_caps);
        SPDLOG_INFO("Using cached GPU capabilities: {}", cached_caps.gpu_name);
    } else {
        SPDLOG_INFO("No cached GPU caps found, requesting from host...");
    }

    // Always send handshake request so host doesn't block waiting for it
    flatbuffers::FlatBufferBuilder builder;

    uint32_t pref_w = 1920;
    uint32_t pref_h = 1080;
#ifdef _WIN32
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw > 0 && sh > 0) {
        pref_w = static_cast<uint32_t>(sw);
        pref_h = static_cast<uint32_t>(sh);
    }
#endif

    flatbuffers::Offset<flatbuffers::String> token_str;
    if (!auth_token.empty())
        token_str = builder.CreateString(auth_token);
    auto req = fbs::CreateCapabilitiesRequest(builder, 1, pref_w, pref_h,
        auth_token.empty() ? 0 : token_str);
    auto msg = fbs::CreateMessage(
        builder, fbs::MessagePayload_CapabilitiesRequest, req.Union());
    builder.Finish(msg);

    auto span = builder.GetBufferSpan();
    uint32_t net_size = htonl(static_cast<uint32_t>(span.size()));

    if (!client.send_data(reinterpret_cast<const uint8_t*>(&net_size),
                           sizeof(net_size)))
        return false;
    if (!client.send_data(span.data(), span.size()))
        return false;

    if (is_cached) {
        // Already have cached capabilities, but must consume host response
        // to keep TCP stream in sync for subsequent messages
        uint32_t resp_size = 0;
        if (!receive_handshake(reinterpret_cast<uint8_t*>(&resp_size),
                               sizeof(resp_size))) {
            SPDLOG_ERROR("Handshake: failed to receive cached capability response size");
            return false;
        }
        resp_size = ntohl(resp_size);
        if (resp_size == 0 || resp_size > kMaxHandshakeBytes) {
            SPDLOG_ERROR("Handshake: invalid response size {}", resp_size);
            return false;
        }
        std::vector<uint8_t> buf(resp_size);
        if (!receive_handshake(buf.data(), resp_size)) return false;
        auto* root = protocol::verify_root(buf.data(), buf.size());
        if (!root || root->payload_type() != fbs::MessagePayload_CapabilitiesResponse) {
            SPDLOG_ERROR("Handshake: unexpected cached capability response");
            return false;
        }
        return true;
    }

    uint32_t resp_size = 0;
    if (!receive_handshake(reinterpret_cast<uint8_t*>(&resp_size),
                           sizeof(resp_size)))
        return false;

    resp_size = ntohl(resp_size);
    if (resp_size == 0 || resp_size > kMaxHandshakeBytes) {
        SPDLOG_ERROR("Handshake: invalid response size {}", resp_size);
        return false;
    }
    std::vector<uint8_t> resp_buf(resp_size);
    if (!receive_handshake(resp_buf.data(), resp_size))
        return false;

    auto* root = protocol::verify_root(resp_buf.data(), resp_buf.size());
    if (!root || root->payload_type() != fbs::MessagePayload_CapabilitiesResponse)
        return false;

    auto* caps = root->payload_as_CapabilitiesResponse();
    if (!caps || !caps->gpu_name()) return false;

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
    // Extended properties
    gpu_caps.vendor_id = caps->vendor_id();
    gpu_caps.device_id = caps->device_id();
    gpu_caps.device_type = caps->device_type();
    gpu_caps.max_framebuffer_width = caps->max_framebuffer_width();
    gpu_caps.max_framebuffer_height = caps->max_framebuffer_height();
    gpu_caps.max_framebuffer_layers = caps->max_framebuffer_layers();
    gpu_caps.max_memory_heaps = caps->max_memory_heaps();
    gpu_caps.memory_heap_size_0 = caps->memory_heap_size_0();
    gpu_caps.memory_heap_size_1 = caps->memory_heap_size_1();
    gpu_caps.heap_0_flags = caps->heap_0_flags();
    gpu_caps.heap_1_flags = caps->heap_1_flags();
    gpu_caps.memory_type_count = caps->memory_type_count();
    gpu_caps.max_sampler_anisotropy = caps->max_sampler_anisotropy();
    gpu_caps.max_color_attachments = caps->max_color_attachments();
    gpu_caps.subgroup_size = caps->subgroup_size();
    gpu_caps.timestamp_period = caps->timestamp_period();
    gpu_caps.max_viewports = caps->max_viewports();
    gpu_caps.max_viewport_dimensions_w = caps->max_viewport_dimensions_w();
    gpu_caps.max_viewport_dimensions_h = caps->max_viewport_dimensions_h();
    gpu_caps.min_uniform_buffer_offset_alignment = caps->min_uniform_buffer_offset_alignment();
    gpu_caps.min_storage_buffer_offset_alignment = caps->min_storage_buffer_offset_alignment();
    gpu_caps.max_uniform_buffer_range = caps->max_uniform_buffer_range();
    gpu_caps.max_storage_buffer_range = caps->max_storage_buffer_range();
    gpu_caps.non_coherent_atom_size = caps->non_coherent_atom_size();
    gpu_caps.buffer_image_granularity = caps->buffer_image_granularity();
    gpu_caps.sample_counts = caps->sample_counts();
    gpu_caps.framebuffer_color_sample_counts = caps->framebuffer_color_sample_counts();

    gpu_caps.max_bound_descriptor_sets_ext = caps->max_bound_descriptor_sets_ext();
    gpu_caps.max_per_stage_descriptor_samplers = caps->max_per_stage_descriptor_samplers();
    gpu_caps.max_per_stage_descriptor_uniform_buffers = caps->max_per_stage_descriptor_uniform_buffers();
    gpu_caps.max_per_stage_descriptor_storage_buffers = caps->max_per_stage_descriptor_storage_buffers();
    gpu_caps.max_per_stage_descriptor_sampled_images = caps->max_per_stage_descriptor_sampled_images();
    gpu_caps.max_per_stage_descriptor_storage_images = caps->max_per_stage_descriptor_storage_images();
    gpu_caps.max_per_stage_resources_ext = caps->max_per_stage_resources_ext();
    gpu_caps.max_compute_work_group_count_x = caps->max_compute_work_group_count_x();
    gpu_caps.max_compute_work_group_count_y = caps->max_compute_work_group_count_y();
    gpu_caps.max_compute_work_group_count_z = caps->max_compute_work_group_count_z();
    gpu_caps.max_compute_work_group_invocations = caps->max_compute_work_group_invocations();
    gpu_caps.max_compute_shared_memory_size = caps->max_compute_shared_memory_size();
    gpu_caps.max_clip_distances = caps->max_clip_distances();
    gpu_caps.max_cull_distances = caps->max_cull_distances();
    gpu_caps.max_combined_clip_and_cull_distances = caps->max_combined_clip_and_cull_distances();
    gpu_caps.max_tessellation_factor = caps->max_tessellation_factor();
    gpu_caps.max_fragment_output_attachments = caps->max_fragment_output_attachments();

    cache.save(gpu_caps);
    caps::store(gpu_caps);

    SPDLOG_INFO("Host GPU capabilities received: {}", gpu_caps.gpu_name);
    return true;
}

bool initialize_guest_internal(const char* host_hint, uint16_t port_hint) {
    init_logger();

    if (host_hint != nullptr && host_hint[0] != ' ') {
        g_host_hint = host_hint;
    }
    if (port_hint != 0) {
        g_port_hint = port_hint;
    }

#ifdef _WIN32
    // Auto-register ICD in HKCU registry so future Vulkan apps
    // find this driver without needing VK_ICD_FILENAMES or the launcher.
    // HKCU requires no admin privileges.
    if (!icd_registry::is_icd_registered()) {
        icd_registry::register_icd();
    }
#endif

    // Initialize hooks FIRST — must be available even without host connection
    SPDLOG_INFO("Initializing Vulkan hooks...");
    intercept::initialize_hooks();

    SPDLOG_INFO("Guest initialized (deferred host connection). Pipeline: OpenGL/OpenCL/Vulkan → omniGPU → Host GPU");
    return true;
}

} // anonymous namespace

bool initialize_guest(const char* host_hint, uint16_t port_hint) {
    std::call_once(g_init_flag, [host_hint, port_hint]() {
        g_init_result = initialize_guest_internal(host_hint, port_hint);
    });
    return g_init_result;
}

bool connect_to_host() {
    std::lock_guard<std::mutex> connect_lock(g_connect_mutex);
    if (g_client != nullptr && g_running.load(std::memory_order_acquire)) {
        return true;
    }

    // Recover from a previously lost connection before attempting a retry.
    if (g_client != nullptr) {
        g_client->interrupt();
        if (g_recv_thread != nullptr) {
            if (g_recv_thread->joinable()) g_recv_thread->join();
            delete g_recv_thread;
            g_recv_thread = nullptr;
        }
        intercept::set_batch(nullptr);
        if (g_batch != nullptr) {
            delete g_batch;
            g_batch = nullptr;
        }
        if (g_decoder != nullptr) {
            g_decoder->shutdown();
            delete g_decoder;
            g_decoder = nullptr;
        }
        g_client->disconnect();
        delete g_client;
        g_client = nullptr;
    }

        auto cfg = config::load();
        if (!cfg.valid) {
            SPDLOG_ERROR("Guest configuration is invalid; refusing to connect");
            return false;
        }
        if (!g_host_hint.empty()) cfg.host = g_host_hint;
        if (g_port_hint != 0) cfg.port = g_port_hint;
        if (cfg.host.empty() || cfg.port == 0) {
            SPDLOG_ERROR("Guest endpoint is invalid; refusing to connect");
            return false;
        }
        std::string host = cfg.host;
        uint16_t port = cfg.port;

        SPDLOG_INFO("OmniGPU Guest connecting to {}:{} via TCP...", host, port);

        g_client = new Client(host, port);
        if (!g_client->connect()) {
            SPDLOG_ERROR("Failed to connect to {}:{} — check host address, port, and firewall", host, port);
            delete g_client;
            g_client = nullptr;
            return false;
        }

        if (!do_handshake(*g_client, cfg.auth_token)) {
            SPDLOG_ERROR("Handshake failed or exceeded its deadline; refusing an unauthenticated or desynchronized session");
            g_client->disconnect();
            delete g_client;
            g_client = nullptr;
            return false;
        }

        g_batch = new batch::CommandBatch(
            g_client,
            cfg.min_batch_commands,
            cfg.min_batch_bytes,
            true,
            cfg.adaptive_batching,
            cfg.max_batch_interval_ms);

        if (cfg.adaptive_batching) {
            SPDLOG_INFO("Adaptive batching enabled (interval={}ms, min_cmd={}, max_cmd={})",
                         cfg.max_batch_interval_ms,
                         cfg.min_batch_commands,
                         cfg.max_batch_commands);
        }

        // Enable command batching to forward Vulkan commands to the host
        intercept::set_batch(g_batch);

        // Publish the decoder before the receive thread can inspect it. The
        // previous order was a C++ data race and could drop the first frame.
        g_decoder = video::create_decoder();
        if (g_decoder) {
            g_decoder->init();
            g_decoder->set_frame_callback([](video::DecodedFrame frame) {
                SPDLOG_DEBUG("Decoded frame: id={}, {}x{}, rgba={} bytes",
                             frame.frame_id, frame.width, frame.height,
                             frame.rgba_pixels.size());
            });
            SPDLOG_INFO("Video decoder initialized (hw={})",
                        g_decoder->hardware_accelerated());
        }

        g_running = true;
        g_recv_thread = new std::thread([]() {
            SPDLOG_INFO("Receive thread started");
            while (g_running) {
                uint32_t msg_size = 0;
                if (!g_client->receive_data(reinterpret_cast<uint8_t*>(&msg_size),
                                             sizeof(msg_size))) {
                    if (g_running) SPDLOG_ERROR("Receive thread: connection lost");
                    g_client->set_sync_response(0);
                    break;
                }
                msg_size = ntohl(msg_size);
                if (msg_size > 64 * 1024 * 1024) {
                    SPDLOG_ERROR("Receive thread: message size {} exceeds 64MB limit", msg_size);
                    g_client->set_sync_response(0);
                    break;
                }
                std::vector<uint8_t> buf(msg_size);
                if (!g_client->receive_data(buf.data(), msg_size)) {
                    SPDLOG_ERROR("Receive thread: failed to read message");
                    g_client->set_sync_response(0);
                    break;
                }
                auto* msg = protocol::verify_root(buf.data(), buf.size());
                if (!msg) continue;
                if (msg->payload_type() == fbs::MessagePayload_DataMessage) {
                    auto* dm = msg->payload_as_DataMessage();
                    if (!dm || !dm->payload()) continue;

                    if (dm->data_type() == fbs::DataType_StorageBuffer) {
                        // Buffer readback: host sent GPU memory contents back
                        uint64_t mem_key = dm->data_id();
                        auto* payload = dm->payload();
                        intercept::update_shadow_buffer(
                            mem_key,
                            payload ? payload->data() : nullptr,
                            payload ? payload->size() : 0,
                            dm->offset());
                    } else if (dm->data_type() == fbs::DataType_Unknown) {
                        uint64_t key = dm->data_id();
                        auto* payload = dm->payload();
                        if (payload) {
                            if (payload->size() == sizeof(VkSubresourceLayout)) {
                                intercept::write_layout_result(key, payload->data(), payload->size());
                            } else {
                                intercept::write_query_results(key, payload->data(), payload->size());
                            }
                        }
                    }
                } else if (msg->payload_type() == fbs::MessagePayload_VideoFrame) {
                    auto* vf = msg->payload_as_VideoFrame();
                    if (vf && g_decoder) {
                        auto* payload = vf->data();
                        g_decoder->decode(
                            static_cast<video::Codec>(vf->codec()),
                            vf->is_keyframe(),
                            payload ? payload->data() : nullptr,
                            payload ? payload->size() : 0,
                            vf->frame_id(), vf->timestamp_ms(),
                            vf->width(), vf->height());
                    }
                }
            }
            g_running.store(false, std::memory_order_release);
            SPDLOG_INFO("Receive thread stopped");
        });
    return g_running.load(std::memory_order_acquire);
}

Client* get_client() {
    return g_client;
}

void shutdown_guest() {
    std::lock_guard<std::mutex> connect_lock(g_connect_mutex);
    g_running = false;

    // Shutdown first to unblock recv(); Client owns the final close.
    if (g_client) {
        g_client->interrupt();
    }

    if (g_recv_thread) {
        if (g_recv_thread->joinable()) g_recv_thread->join();
        delete g_recv_thread;
        g_recv_thread = nullptr;
    }

    intercept::set_batch(nullptr);
    if (g_batch) {
        g_batch->force_flush();
        delete g_batch;
        g_batch = nullptr;
    }

    if (g_decoder) {
        g_decoder->shutdown();
        delete g_decoder;
        g_decoder = nullptr;
    }

    intercept::shutdown_hooks();
    if (g_client) {
        g_client->disconnect();
        delete g_client;
        g_client = nullptr;
    }
    SPDLOG_INFO("OmniGPU Guest shut down");
}

} // namespace omnigpu::init
