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
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace omnigpu::init {

namespace {

Client* g_client = nullptr;
batch::CommandBatch* g_batch = nullptr;
video::VideoDecoder* g_decoder = nullptr;
std::thread* g_recv_thread = nullptr;
std::atomic<bool> g_running{false};
std::once_flag g_init_flag;
bool g_init_result = false;
bool g_is_compute = false;

static void recv_thread_main() {
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
        if (!msg) { SPDLOG_TRACE("Recv thread: verify_root failed"); continue; }
        if (msg->payload_type() == fbs::MessagePayload_DataMessage) {
            auto* dm = msg->payload_as_DataMessage();
            if (!dm || !dm->payload()) { SPDLOG_TRACE("Recv thread: DataMessage null"); continue; }

            if (dm->data_type() == fbs::DataType_StorageBuffer) {
                uint64_t mem_key = dm->data_id();
                auto* payload = dm->payload();
                SPDLOG_TRACE("Recv thread: StorageBuffer mem={:#x} size={}", mem_key, payload ? payload->size() : 0);
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
                        g_client->set_sync_response_buf(payload->data(), payload->size());
                    } else {
                        intercept::write_query_results(key, payload->data(), payload->size());
                        g_client->set_sync_response_buf(payload->data(), payload->size());
                    }
                }
            }
        } else if (msg->payload_type() == fbs::MessagePayload_VideoFrame) {
            auto* vf = msg->payload_as_VideoFrame();
            if (vf && g_decoder) {
                auto* payload = vf->data();
                auto* pdata = payload ? payload->data() : nullptr;
                size_t psz = payload ? payload->size() : 0;
                if (pdata && psz > 0) {
                    g_decoder->decode(
                        static_cast<video::Codec>(vf->codec()),
                        vf->is_keyframe(),
                        pdata, psz,
                        vf->frame_id(), vf->timestamp_ms(),
                        vf->width(), vf->height());
                } else {
                    SPDLOG_DEBUG("Recv thread: empty VideoFrame (id={}) skipped", vf->frame_id());
                }
            }
        }
    }
    SPDLOG_INFO("Receive thread stopped");
}

static DWORD recv_thread_wrapper() {
    __try {
        recv_thread_main();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        SPDLOG_CRITICAL("Receive thread CRASHED: exception code={:#x}", code);

        HANDLE hProcess = GetCurrentProcess();
        SymInitialize(hProcess, NULL, TRUE);

        void* stack[32];
        USHORT frames = CaptureStackBackTrace(0, 32, stack, NULL);

        SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256, 1);
        if (symbol) {
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            for (USHORT i = 0; i < frames; i++) {
                DWORD64 displacement = 0;
                if (SymFromAddr(hProcess, (DWORD64)stack[i], &displacement, symbol)) {
                    SPDLOG_CRITICAL("  [{}] {}+0x{:x}", i, symbol->Name, (unsigned)displacement);
                } else {
                    SPDLOG_CRITICAL("  [{}] {}", i, stack[i]);
                }
            }
            free(symbol);
        }
        SymCleanup(hProcess);
        g_client->set_sync_response(0);
    }
    return 0;
}

bool do_handshake(Client& client, const std::string& auth_token) {
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

    bool is_compute = (getenv("GGML_VK_FORCE_MAX_BUFFER_SIZE") != nullptr)
                   || (getenv("OMNIGPU_COMPUTE") != nullptr)
                   || (getenv("GGML_CUDA_NO_PINNED") != nullptr)
                   || (getenv("GGML_OPENCL_NO_BINARY") != nullptr)
                   || (getenv("GGML_SYCL_NO_MULTI_BACKEND") != nullptr);
    g_is_compute = is_compute;

    auto req = fbs::CreateCapabilitiesRequest(builder, 1, pref_w, pref_h,
        auth_token.empty() ? 0 : token_str, is_compute, is_compute);
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
        // Must consume host response to keep TCP stream in sync.
        // Also update ML support flags from host response (not in cache or stale).
        uint32_t resp_size = 0;
        if (client.receive_data(reinterpret_cast<uint8_t*>(&resp_size),
                                 sizeof(resp_size))) {
            resp_size = ntohl(resp_size);
            if (resp_size > 64 * 1024 * 1024) {
                SPDLOG_ERROR("Handshake: response size {} exceeds limit", resp_size);
                return false;
            }
            std::vector<uint8_t> buf(resp_size);
            if (!client.receive_data(buf.data(), resp_size)) return false;
            auto* root = protocol::verify_root(buf.data(), buf.size());
            if (root && root->payload_type() == fbs::MessagePayload_CapabilitiesResponse) {
                auto* caps_resp = root->payload_as_CapabilitiesResponse();
                if (caps_resp && caps_resp->gpu_name()) {
                    caps::GpuCapabilities updated = cached_caps;
                    updated.supports_16bit_storage = caps_resp->supports_16bit_storage();
                    updated.supports_8bit_storage = caps_resp->supports_8bit_storage();
                    updated.supports_float16_int8 = caps_resp->supports_float16_int8();
                    updated.supports_cooperative_matrix = caps_resp->supports_cooperative_matrix();
                    updated.coopmat_m = caps_resp->coopmat_m();
                    updated.coopmat_n = caps_resp->coopmat_n();
                    updated.coopmat_k = caps_resp->coopmat_k();
                    updated.supports_integer_dot_product = caps_resp->supports_integer_dot_product();
                    caps::store(updated);
                    SPDLOG_INFO("Updated ML support flags from host response");
                }
            } else {
                SPDLOG_WARN("Handshake: unexpected response type, ignoring");
            }
        }
        return true;
    }

    uint32_t resp_size = 0;
    if (!client.receive_data(reinterpret_cast<uint8_t*>(&resp_size),
                              sizeof(resp_size)))
        return false;

    resp_size = ntohl(resp_size);
    std::vector<uint8_t> resp_buf(resp_size);
    if (!client.receive_data(resp_buf.data(), resp_size))
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

    // ===== ML support flags (Phase 1) =====
    gpu_caps.supports_16bit_storage = caps->supports_16bit_storage();
    gpu_caps.supports_8bit_storage  = caps->supports_8bit_storage();
    gpu_caps.supports_float16_int8  = caps->supports_float16_int8();

    // ===== Cooperative matrix (Phase 2) =====
    gpu_caps.supports_cooperative_matrix = caps->supports_cooperative_matrix();
    gpu_caps.coopmat_m = caps->coopmat_m();
    gpu_caps.coopmat_n = caps->coopmat_n();
    gpu_caps.coopmat_k = caps->coopmat_k();
    gpu_caps.supports_integer_dot_product = caps->supports_integer_dot_product();

    cache.save(gpu_caps);
    caps::store(gpu_caps);

    SPDLOG_INFO("Host GPU capabilities received: {}", gpu_caps.gpu_name);
    return true;
}

bool initialize_guest_internal(const char* host_hint, uint16_t port_hint) {
    init_logger();

    auto cfg = config::load();

#ifdef _WIN32
    // Auto-register ICD in HKCU registry so future Vulkan apps
    // find this driver without needing VK_ICD_FILENAMES or the launcher.
    // HKCU requires no admin privileges.
    if (!icd_registry::is_icd_registered()) {
        icd_registry::register_icd();
    }
#endif

    // Workaround: set GGML_VK_FORCE_MAX_BUFFER_SIZE if not already set,
    // to avoid maxBufferSize=0 from unhandled VkPhysicalDeviceMaintenance4Properties
    if (!getenv("GGML_VK_FORCE_MAX_BUFFER_SIZE")) {
#ifdef _WIN32
        _putenv_s("GGML_VK_FORCE_MAX_BUFFER_SIZE", "8589934592");
#else
        setenv("GGML_VK_FORCE_MAX_BUFFER_SIZE", "8589934592", 0);
#endif
    }
    if (!getenv("GGML_VK_FORCE_MAX_ALLOCATION_SIZE")) {
#ifdef _WIN32
        _putenv_s("GGML_VK_FORCE_MAX_ALLOCATION_SIZE", "8589934592");
#else
        setenv("GGML_VK_FORCE_MAX_ALLOCATION_SIZE", "8589934592", 0);
#endif
    }
    if (!getenv("GGML_VK_SUBALLOCATION_BLOCK_SIZE")) {
#ifdef _WIN32
        _putenv_s("GGML_VK_SUBALLOCATION_BLOCK_SIZE", "1073741824");
#else
        setenv("GGML_VK_SUBALLOCATION_BLOCK_SIZE", "1073741824", 0);
#endif
    }

    // Initialize hooks FIRST — must be available even without host connection
    SPDLOG_INFO("Initializing Vulkan hooks...");
    SPDLOG_INFO("BUILD: sync_all_safety_v2 max_256mb");  // Marker
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
    static bool connected = false;
    if (connected && g_client) return true;

    auto cfg = config::load();
    std::string host = cfg.host;
    uint16_t port = cfg.port;

    SPDLOG_INFO("OmniGPU Guest connecting to {}:{} via TCP...", host, port);

    // Cleanup previous connection if any
    if (g_client) {
        delete g_client;
        g_client = nullptr;
    }

    g_client = new Client(host, port);
    if (!g_client->connect()) {
        SPDLOG_ERROR("Failed to connect to {}:{} — check host address, port, and firewall", host, port);
        delete g_client;
        g_client = nullptr;
        connected = false;
        return false;
    }

    if (!do_handshake(*g_client, cfg.auth_token)) {
        SPDLOG_ERROR("Handshake failed — host rejected connection or auth failed");
        delete g_client;
        g_client = nullptr;
        connected = false;
        return false;
    }

    size_t min_cmd = g_is_compute ? 64 : cfg.min_batch_commands;
    size_t min_bytes = g_is_compute ? 65536 : cfg.min_batch_bytes;
    uint32_t interval = g_is_compute ? 2 : cfg.max_batch_interval_ms;
    g_batch = new batch::CommandBatch(
        g_client,
        min_cmd,
        min_bytes,
        true,
        cfg.adaptive_batching,
        interval);

    if (cfg.adaptive_batching) {
        SPDLOG_INFO("Adaptive batching enabled (interval={}ms, min_cmd={}, max_cmd={})",
                     interval, min_cmd, g_is_compute ? 1024 : cfg.max_batch_commands);
    } else {
        SPDLOG_INFO("Batching: {}ms interval, {} min commands", interval, min_cmd);
    }

    intercept::set_batch(g_batch);

    g_client->set_rtt_callback([batch = g_batch](uint32_t rtt_ms) {
        batch->record_latency_sample(rtt_ms);
    });

    g_running = true;
    g_recv_thread = new std::thread([]() {
        SPDLOG_INFO("Receive thread started");
        HRESULT coHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(coHr)) SPDLOG_WARN("Receive thread: CoInitializeEx failed (0x{:08X})", coHr);
        recv_thread_wrapper();
        if (SUCCEEDED(coHr)) CoUninitialize();
    });

    g_decoder = video::create_decoder();
    if (g_decoder) {
        g_decoder->init();
        g_decoder->set_frame_callback([](video::DecodedFrame frame) {
            SPDLOG_DEBUG("Decoded frame: id={}, {}x{}, rgba={} bytes",
                         frame.frame_id, frame.width, frame.height,
                         frame.rgba_pixels.size());
        });
        SPDLOG_INFO("Video decoder initialized (hw={})", g_decoder->hardware_accelerated());
    }

    connected = true;
    return true;
}

Client* get_client() {
    return g_client;
}

void shutdown_guest() {
    g_running = false;

    // Flush any pending commands before shutdown
    if (g_batch) {
        g_batch->force_flush();
    }

    // Gracefully shutdown socket to unblock recv() in receive thread
    if (g_client && g_client->socket() != INVALID_SOCKET) {
        ::shutdown(g_client->socket(), SD_BOTH);
    }

    // Join receive thread (now unblocked by shutdown)
    if (g_recv_thread) {
        if (g_recv_thread->joinable()) g_recv_thread->join();
        delete g_recv_thread;
        g_recv_thread = nullptr;
    }

    // Now safe to destroy batch and decoder
    if (g_batch) {
        delete g_batch;
        g_batch = nullptr;
    }

    if (g_decoder) {
        g_decoder->shutdown();
        delete g_decoder;
        g_decoder = nullptr;
    }

    intercept::shutdown_hooks();

    // Close socket after threads are done
    if (g_client) {
        g_client->disconnect();
        delete g_client;
        g_client = nullptr;
    }
    SPDLOG_INFO("OmniGPU Guest shut down");
}

} // namespace omnigpu::init
