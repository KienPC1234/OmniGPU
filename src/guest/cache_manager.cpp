#include "cache_manager.h"
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace omnigpu::cache {

using json = nlohmann::json;

CacheManager::CacheManager(const std::string& host, uint16_t port)
    : cache_key_(host + "_" + std::to_string(port)),
      cache_path_(get_cache_path()) {}

std::string CacheManager::get_cache_path() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", path, sizeof(path))) {
        return std::string(path) + "\\omnigpu\\" + kCacheFileName;
    }
    return kCacheFileName;
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/omnigpu/" + kCacheFileName;
    }
    return kCacheFileName;
#endif
}

bool CacheManager::load(caps::GpuCapabilities& caps) const {
    std::ifstream file(cache_path_);
    if (!file.is_open()) {
        SPDLOG_DEBUG("Cache file not found: {}", cache_path_);
        return false;
    }

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        SPDLOG_WARN("Failed to parse cache file: {}", cache_path_);
        return false;
    }

    auto entry = j.find(cache_key_);
    if (entry == j.end()) {
        SPDLOG_DEBUG("No cache entry for {}", cache_key_);
        return false;
    }

    auto& data = *entry;
    uint64_t saved_ts = data.value("timestamp", 0ULL);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    if (static_cast<uint64_t>(now) - saved_ts > kCacheTtlSeconds) {
        SPDLOG_INFO("Cache expired for {} ({}s old)", cache_key_,
                    static_cast<uint64_t>(now) - saved_ts);
        return false;
    }

    caps.gpu_name = data.value("gpu_name", "");
    caps.driver_version = data.value("driver_version", 0U);
    caps.api_version = data.value("api_version", 0U);
    caps.max_memory_allocation = data.value("max_memory_allocation", 4096ULL);
    caps.max_memory_allocation_size = data.value("max_memory_allocation_size", UINT64_MAX);
    caps.max_push_constants_size = data.value("max_push_constants_size", 128U);
    caps.max_bound_descriptor_sets = data.value("max_bound_descriptor_sets", 4U);
    caps.max_per_stage_resources = data.value("max_per_stage_resources", 32U);
    caps.max_image_dimension_2d = data.value("max_image_dimension_2d", 8192U);
    caps.timestamp = saved_ts;
    caps.vendor_id = data.value("vendor_id", 0x10DEU);
    caps.device_id = data.value("device_id", 0x2684U);
    caps.device_type = data.value("device_type", 2U);
    caps.max_framebuffer_width = data.value("max_fb_w", 16384U);
    caps.max_framebuffer_height = data.value("max_fb_h", 16384U);
    caps.max_memory_heaps = data.value("mem_heaps", 2U);
    caps.memory_heap_size_0 = data.value("mem_heap_0", 24ULL * 1024 * 1024 * 1024);
    caps.memory_heap_size_1 = data.value("mem_heap_1", 16ULL * 1024 * 1024 * 1024);
    caps.memory_type_count = data.value("mem_types", 4U);
    caps.max_sampler_anisotropy = data.value("max_aniso", 16.0f);
    caps.subgroup_size = data.value("subgroup", 32U);

    caps.max_bound_descriptor_sets_ext = data.value("max_bound_descriptor_sets_ext", 32U);
    caps.max_per_stage_descriptor_samplers = data.value("max_per_stage_descriptor_samplers", 2048U);
    caps.max_per_stage_descriptor_uniform_buffers = data.value("max_per_stage_descriptor_uniform_buffers", 2048U);
    caps.max_per_stage_descriptor_storage_buffers = data.value("max_per_stage_descriptor_storage_buffers", 2048U);
    caps.max_per_stage_descriptor_sampled_images = data.value("max_per_stage_descriptor_sampled_images", 2048U);
    caps.max_per_stage_descriptor_storage_images = data.value("max_per_stage_descriptor_storage_images", 2048U);
    caps.max_per_stage_resources_ext = data.value("max_per_stage_resources_ext", 1000000U);
    caps.max_compute_work_group_count_x = data.value("max_compute_work_group_count_x", 65535U);
    caps.max_compute_work_group_count_y = data.value("max_compute_work_group_count_y", 65535U);
    caps.max_compute_work_group_count_z = data.value("max_compute_work_group_count_z", 65535U);
    caps.max_compute_work_group_invocations = data.value("max_compute_work_group_invocations", 1024U);
    caps.max_compute_shared_memory_size = data.value("max_compute_shared_memory_size", 49152U);
    caps.max_clip_distances = data.value("max_clip_distances", 8U);
    caps.max_cull_distances = data.value("max_cull_distances", 8U);
    caps.max_combined_clip_and_cull_distances = data.value("max_combined_clip_and_cull_distances", 8U);
    caps.max_tessellation_factor = data.value("max_tessellation_factor", 64U);
    caps.max_fragment_output_attachments = data.value("max_fragment_output_attachments", 8U);

    // ML & Subgroup support fields (Phase 1 + 2)
    uint32_t all_ops = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                       VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
                       VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                       VK_SUBGROUP_FEATURE_CLUSTERED_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT;
    caps.supported_subgroup_operations = data.value("supported_subgroup_ops", all_ops);
    caps.supports_16bit_storage = data.value("supports_16bit_storage", true);
    caps.supports_8bit_storage  = data.value("supports_8bit_storage", true);
    caps.supports_float16_int8  = data.value("supports_float16_int8", true);
    caps.supports_cooperative_matrix = data.value("supports_cooperative_matrix", true);
    caps.coopmat_m = data.value("coopmat_m", 16U);
    caps.coopmat_n = data.value("coopmat_n", 16U);
    caps.coopmat_k = data.value("coopmat_k", 16U);
    caps.supports_integer_dot_product = data.value("supports_integer_dot_product", true);

    SPDLOG_INFO("Loaded cached GPU caps for {}: {}", cache_key_,
                caps.gpu_name);
    return true;
}

bool CacheManager::save(const caps::GpuCapabilities& caps) {
    // Ensure directory exists
    auto dir = cache_path_.substr(0, cache_path_.find_last_of("/\\"));
    if (!dir.empty()) {
#ifdef _WIN32
        CreateDirectoryA(dir.c_str(), nullptr);
#else
        mkdir(dir.c_str(), 0755);
#endif
    }

    json j;
    std::ifstream infile(cache_path_);
    if (infile.is_open()) {
        json parsed = json::parse(infile, nullptr, false);
        if (!parsed.is_discarded()) {
            j = std::move(parsed);
        }
    }

    json entry;
    entry["gpu_name"] = caps.gpu_name;
    entry["driver_version"] = caps.driver_version;
    entry["api_version"] = caps.api_version;
    entry["max_memory_allocation"] = caps.max_memory_allocation;
    entry["max_memory_allocation_size"] = caps.max_memory_allocation_size;
    entry["max_push_constants_size"] = caps.max_push_constants_size;
    entry["max_bound_descriptor_sets"] = caps.max_bound_descriptor_sets;
    entry["max_per_stage_resources"] = caps.max_per_stage_resources;
    entry["max_image_dimension_2d"] = caps.max_image_dimension_2d;
    entry["timestamp"] = caps.timestamp;
    entry["vendor_id"] = caps.vendor_id;
    entry["device_id"] = caps.device_id;
    entry["device_type"] = caps.device_type;
    entry["max_fb_w"] = caps.max_framebuffer_width;
    entry["max_fb_h"] = caps.max_framebuffer_height;
    entry["mem_heaps"] = caps.max_memory_heaps;
    entry["mem_heap_0"] = caps.memory_heap_size_0;
    entry["mem_heap_1"] = caps.memory_heap_size_1;
    entry["mem_types"] = caps.memory_type_count;
    entry["max_aniso"] = caps.max_sampler_anisotropy;
    entry["subgroup"] = caps.subgroup_size;

    entry["max_bound_descriptor_sets_ext"] = caps.max_bound_descriptor_sets_ext;
    entry["max_per_stage_descriptor_samplers"] = caps.max_per_stage_descriptor_samplers;
    entry["max_per_stage_descriptor_uniform_buffers"] = caps.max_per_stage_descriptor_uniform_buffers;
    entry["max_per_stage_descriptor_storage_buffers"] = caps.max_per_stage_descriptor_storage_buffers;
    entry["max_per_stage_descriptor_sampled_images"] = caps.max_per_stage_descriptor_sampled_images;
    entry["max_per_stage_descriptor_storage_images"] = caps.max_per_stage_descriptor_storage_images;
    entry["max_per_stage_resources_ext"] = caps.max_per_stage_resources_ext;
    entry["max_compute_work_group_count_x"] = caps.max_compute_work_group_count_x;
    entry["max_compute_work_group_count_y"] = caps.max_compute_work_group_count_y;
    entry["max_compute_work_group_count_z"] = caps.max_compute_work_group_count_z;
    entry["max_compute_work_group_invocations"] = caps.max_compute_work_group_invocations;
    entry["max_compute_shared_memory_size"] = caps.max_compute_shared_memory_size;
    entry["max_clip_distances"] = caps.max_clip_distances;
    entry["max_cull_distances"] = caps.max_cull_distances;
    entry["max_combined_clip_and_cull_distances"] = caps.max_combined_clip_and_cull_distances;
    entry["max_tessellation_factor"] = caps.max_tessellation_factor;
    entry["max_fragment_output_attachments"] = caps.max_fragment_output_attachments;

    // ML support fields (Phase 1 + 2)
    entry["supports_16bit_storage"] = caps.supports_16bit_storage;
    entry["supports_8bit_storage"]  = caps.supports_8bit_storage;
    entry["supports_float16_int8"]  = caps.supports_float16_int8;
    entry["supports_cooperative_matrix"] = caps.supports_cooperative_matrix;
    entry["coopmat_m"] = caps.coopmat_m;
    entry["coopmat_n"] = caps.coopmat_n;
    entry["coopmat_k"] = caps.coopmat_k;
    entry["supports_integer_dot_product"] = caps.supports_integer_dot_product;

    j[cache_key_] = entry;

    std::ofstream outfile(cache_path_);
    if (!outfile.is_open()) {
        SPDLOG_ERROR("Failed to write cache: {}", cache_path_);
        return false;
    }
    outfile << j.dump(2) << std::endl;

    SPDLOG_INFO("Cached GPU caps for {}: {}", cache_key_, caps.gpu_name);
    return true;
}

bool CacheManager::is_valid() const {
    caps::GpuCapabilities dummy;
    return load(dummy);
}

void CacheManager::invalidate() {
    std::ifstream infile(cache_path_);
    if (!infile.is_open()) return;

    json j = json::parse(infile, nullptr, false);
    if (j.is_discarded()) return;

    j.erase(cache_key_);

    std::ofstream outfile(cache_path_);
    outfile << j.dump(2) << std::endl;
}

} // namespace omnigpu::cache
