#pragma once

#include "vulkan_deserializer.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "omnigpu_protocol_generated.h"

namespace omnigpu::host {

class ResourceMapper {
public:
    void set_device(VkDevice dev, VkQueue q, uint32_t qfam, VkCommandPool pool) {
        device_ = dev; queue_ = q; queueFamily_ = qfam; cmdPool_ = pool;
    }

    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queue_family() const { return queueFamily_; }
    VkCommandPool command_pool() const { return cmdPool_; }

    void set_active_cmd(VkCommandBuffer cb) { activeCmd_ = cb; }
    VkCommandBuffer active_cmd() const { return activeCmd_; }

    template<typename Map, typename Key, typename Value>
    void store_impl(Map& map, Key key, Value value, const char* name) {
        auto it = map.find(key);
        if (it != map.end() && it->second != value && it->second != 0) {
            SPDLOG_WARN("ResourceMapper: {} overwriting existing handle {:#x}", name, static_cast<uint64_t>(key));
        }
        map[key] = value;
    }

    void store_instance(uint64_t g, VkInstance v) { store_impl(instances_, g, v, "instance"); }
    void store_device(uint64_t g, VkDevice v) { store_impl(devices_, g, v, "device"); }
    void store_queue(uint64_t g, VkQueue v) { store_impl(queues_, g, v, "queue"); }
    void store_command_pool(uint64_t g, VkCommandPool v) { store_impl(cmdPools_, g, v, "command_pool"); }
    void store_command_buffer(uint64_t g, VkCommandBuffer v) { store_impl(cmdBufs_, g, v, "command_buffer"); }
    void store_buffer(uint64_t g, VkBuffer v) { store_impl(buffers_, g, v, "buffer"); }
    void store_image(uint64_t g, VkImage v) { store_impl(images_, g, v, "image"); }
    void store_image_view(uint64_t g, VkImageView v) { store_impl(imageViews_, g, v, "image_view"); }
    void store_view_image(uint64_t gView, VkImage hostImg) { store_impl(viewImages_, gView, hostImg, "view_image"); }
    VkImage get_view_image(uint64_t gView) const { return lookup(viewImages_, gView); }
    void store_buffer_view(uint64_t g, VkBufferView v) { store_impl(bufferViews_, g, v, "buffer_view"); }
    VkBufferView get_buffer_view(uint64_t g) const { return lookup(bufferViews_, g); }
    void remove_buffer_view(uint64_t g) { bufferViews_.erase(g); }
    void store_sampler(uint64_t g, VkSampler v) { store_impl(samplers_, g, v, "sampler"); }
    void store_shader_module(uint64_t g, VkShaderModule v) { store_impl(shaderModules_, g, v, "shader_module"); }
    void store_pipeline_layout(uint64_t g, VkPipelineLayout v) { store_impl(pipelineLayouts_, g, v, "pipeline_layout"); }
    void store_pipeline(uint64_t g, VkPipeline v) { store_impl(pipelines_, g, v, "pipeline"); }
    void store_pipeline_cache(uint64_t g, VkPipelineCache v) { store_impl(pipelineCaches_, g, v, "pipeline_cache"); }
    void store_render_pass(uint64_t g, VkRenderPass v) { store_impl(renderPasses_, g, v, "render_pass"); }
    void store_framebuffer(uint64_t g, VkFramebuffer v) { store_impl(framebuffers_, g, v, "framebuffer"); }
    void store_descriptor_set_layout(uint64_t g, VkDescriptorSetLayout v) { store_impl(dsls_, g, v, "dsl"); }
    void store_descriptor_pool(uint64_t g, VkDescriptorPool v) { store_impl(dps_, g, v, "descriptor_pool"); }
    void store_descriptor_set(uint64_t g, VkDescriptorSet v) { store_impl(dss_, g, v, "descriptor_set"); }
    void store_fence(uint64_t g, VkFence v) { store_impl(fences_, g, v, "fence"); }
    void store_semaphore(uint64_t g, VkSemaphore v) { store_impl(semaphores_, g, v, "semaphore"); }
    void store_event(uint64_t g, VkEvent v) { store_impl(events_, g, v, "event"); }
    void store_query_pool(uint64_t g, VkQueryPool v) { store_impl(queryPools_, g, v, "query_pool"); }
    void store_private_data_slot(uint64_t g, VkPrivateDataSlot v) { store_impl(privateDataSlots_, g, v, "private_data_slot"); }
    void store_device_memory(uint64_t g, VkDeviceMemory v) { store_impl(memories_, g, v, "device_memory"); }
    void remove_device_memory(uint64_t g) { memories_.erase(g); }
    void remove_instance(uint64_t g) { instances_.erase(g); }
    void remove_device(uint64_t g) { devices_.erase(g); }
    void remove_queue(uint64_t g) { queues_.erase(g); }
    void remove_command_pool(uint64_t g) { cmdPools_.erase(g); }
    void remove_command_buffer(uint64_t g) { cmdBufs_.erase(g); }
    void remove_buffer(uint64_t g) { buffers_.erase(g); }
    void remove_image(uint64_t g) { images_.erase(g); }
    void remove_image_view(uint64_t g) { imageViews_.erase(g); viewImages_.erase(g); }
    void remove_sampler(uint64_t g) { samplers_.erase(g); }
    void remove_shader_module(uint64_t g) { shaderModules_.erase(g); }
    void remove_pipeline_layout(uint64_t g) { pipelineLayouts_.erase(g); }
    void remove_pipeline(uint64_t g) { pipelines_.erase(g); }
    void remove_pipeline_cache(uint64_t g) { pipelineCaches_.erase(g); }
    void remove_render_pass(uint64_t g) { renderPasses_.erase(g); }
    void remove_framebuffer(uint64_t g) { framebuffers_.erase(g); }
    void remove_dsl(uint64_t g) { dsls_.erase(g); }
    void remove_dp(uint64_t g) { dps_.erase(g); }
    void remove_ds(uint64_t g) { dss_.erase(g); }
    void remove_fence(uint64_t g) { fences_.erase(g); }
    void remove_semaphore(uint64_t g) { semaphores_.erase(g); }
    void remove_event(uint64_t g) { events_.erase(g); }
    void remove_query_pool(uint64_t g) { queryPools_.erase(g); }
    void remove_private_data_slot(uint64_t g) { privateDataSlots_.erase(g); }
    void remove_descriptor_update_template(uint64_t g) { duts_.erase(g); }
    void store_descriptor_update_template(uint64_t g, VkDescriptorUpdateTemplate v) { duts_[g] = v; }

    VkCommandPool    get_command_pool(uint64_t g) const { return lookup(cmdPools_, g); }
    VkQueue          get_queue(uint64_t g) const { return lookup(queues_, g); }
    VkCommandBuffer  get_command_buffer(uint64_t g) const { return lookup(cmdBufs_, g); }
    VkBuffer         get_buffer(uint64_t g) const { return lookup(buffers_, g); }
    VkImage          get_image(uint64_t g) const { return lookup(images_, g); }
    VkImageView      get_image_view(uint64_t g) const { return lookup(imageViews_, g); }
    VkSampler        get_sampler(uint64_t g) const { return lookup(samplers_, g); }
    VkShaderModule   get_shader_module(uint64_t g) const { return lookup(shaderModules_, g); }
    VkPipelineLayout get_pipeline_layout(uint64_t g) const { return lookup(pipelineLayouts_, g); }
    VkPipeline       get_pipeline(uint64_t g) const { return lookup(pipelines_, g); }
    VkPipelineCache  get_pipeline_cache(uint64_t g) const { return lookup(pipelineCaches_, g); }
    VkRenderPass     get_render_pass(uint64_t g) const { return lookup(renderPasses_, g); }
    VkFramebuffer    get_framebuffer(uint64_t g) const { return lookup(framebuffers_, g); }
    VkDescriptorSetLayout get_dsl(uint64_t g) const { return lookup(dsls_, g); }
    VkDescriptorPool      get_dp(uint64_t g) const { return lookup(dps_, g); }
    VkDescriptorSet       get_ds(uint64_t g) const { return lookup(dss_, g); }
    VkFence          get_fence(uint64_t g) const { return lookup(fences_, g); }
    VkSemaphore      get_semaphore(uint64_t g) const { return lookup(semaphores_, g); }
    VkEvent          get_event(uint64_t g) const { return lookup(events_, g); }
    VkQueryPool      get_query_pool(uint64_t g) const { return lookup(queryPools_, g); }
    VkDeviceMemory   get_device_memory(uint64_t g) const { return lookup(memories_, g); }
    VkDescriptorUpdateTemplate get_descriptor_update_template(uint64_t g) const { return lookup(duts_, g); }
    VkPrivateDataSlot get_private_data_slot(uint64_t g) const { return lookup(privateDataSlots_, g); }

    void cleanup();

private:
    template<typename T>
    T lookup(const std::unordered_map<uint64_t, T>& map, uint64_t g) const {
        auto it = map.find(g);
        return it != map.end() ? it->second : VK_NULL_HANDLE;
    }

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkCommandBuffer activeCmd_ = VK_NULL_HANDLE;

    std::unordered_map<uint64_t, VkInstance> instances_;
    std::unordered_map<uint64_t, VkDevice> devices_;
    std::unordered_map<uint64_t, VkQueue> queues_;
    std::unordered_map<uint64_t, VkCommandPool> cmdPools_;
    std::unordered_map<uint64_t, VkCommandBuffer> cmdBufs_;
    std::unordered_map<uint64_t, VkBuffer> buffers_;
    std::unordered_map<uint64_t, VkImage> images_;
    std::unordered_map<uint64_t, VkBufferView> bufferViews_;
    std::unordered_map<uint64_t, VkImageView> imageViews_;
    std::unordered_map<uint64_t, VkImage> viewImages_;
    std::unordered_map<uint64_t, VkSampler> samplers_;
    std::unordered_map<uint64_t, VkShaderModule> shaderModules_;
    std::unordered_map<uint64_t, VkPipelineLayout> pipelineLayouts_;
    std::unordered_map<uint64_t, VkPipeline> pipelines_;
    std::unordered_map<uint64_t, VkPipelineCache> pipelineCaches_;
    std::unordered_map<uint64_t, VkRenderPass> renderPasses_;
    std::unordered_map<uint64_t, VkFramebuffer> framebuffers_;
    std::unordered_map<uint64_t, VkDescriptorSetLayout> dsls_;
    std::unordered_map<uint64_t, VkDescriptorPool> dps_;
    std::unordered_map<uint64_t, VkDescriptorSet> dss_;
    std::unordered_map<uint64_t, VkFence> fences_;
    std::unordered_map<uint64_t, VkSemaphore> semaphores_;
    std::unordered_map<uint64_t, VkEvent> events_;
    std::unordered_map<uint64_t, VkQueryPool> queryPools_;
    std::unordered_map<uint64_t, VkPrivateDataSlot> privateDataSlots_;
    std::unordered_map<uint64_t, VkDeviceMemory> memories_;
    std::unordered_map<uint64_t, VkDescriptorUpdateTemplate> duts_;
};

class CommandDispatcher {
public:
    CommandDispatcher();
    ~CommandDispatcher();

    void set_device(VkPhysicalDevice physDev, VkDevice device, VkQueue queue,
                    uint32_t queueFamily, VkCommandPool cmdPool);
    VkPhysicalDevice phys_device() const { return physDev_; }
    void set_framebuffer_size(uint32_t w, uint32_t h);

    void dispatch(fbs::FunctionId func_id, const uint8_t* args, size_t args_size);
    bool flush_and_readback(std::vector<uint8_t>& out_pixels);

    bool begin_render_pass(VkRenderPass rp, VkFramebuffer fb,
                           uint32_t w, uint32_t h);
    void end_render_pass();

    bool setup_framebuffer();

    void cleanup();

    ResourceMapper& mapper() { return mapper_; }

    // VRAM budget (0 = unlimited)
    void set_vram_budget(uint64_t budget) { vramBudget_ = budget; }
    void set_compute_mode(bool cm) { isComputeMode_ = cm; }
    void readback_all_buffers();
    uint64_t vram_used() const { return vramUsed_; }

    // Callback for sending data back to guest (needed for readback)
    using SendDataFn = std::function<bool(uint64_t buffer_id, const uint8_t* data, size_t size, VkDeviceSize offset)>;
    void set_send_data_callback(SendDataFn fn) { sendDataFn_ = fn; }

    // Public accessor for cached function pointers (needed by Session)
    void* get_pfn_semaphore_counter_value() const { return pfnGetSemaphoreCounterValue_; }

private:
    using HandlerFn = std::function<void(CommandDispatcher&, VulkanDeserializer&)>;
    std::unordered_map<int, HandlerFn> handlers_;
    ResourceMapper mapper_;

    VkPhysicalDevice physDev_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer mainFramebuffer_ = VK_NULL_HANDLE;
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkImage renderTargetImage_ = VK_NULL_HANDLE;
    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    uint32_t fbWidth_ = 800, fbHeight_ = 600;
    bool inRenderPass_ = false;

    VkFence pendingSubmitFence_ = VK_NULL_HANDLE;
    uint64_t pendingSubmitFenceGuestHandle_ = 0;
    bool hasPendingSubmit_ = false;

    // VRAM budget tracking
    uint64_t vramBudget_ = 0;
    uint64_t vramUsed_ = 0;
    std::unordered_map<uint64_t, uint64_t> memorySizes_;
    bool isComputeMode_ = false;

    // Per-framebuffer render target image (maps guest framebuffer handle → image)
    std::unordered_map<uint64_t, VkImage> framebufferRenderTarget_;

    SendDataFn sendDataFn_;

    // Cached function pointers (void* to avoid VK header dep issues)
    void* pfnCmdPipelineBarrier2_ = nullptr;
    void* pfnCmdCopyBuffer2_ = nullptr;
    void* pfnCmdCopyImage2_ = nullptr;
    void* pfnCmdCopyBufferToImage2_ = nullptr;
    void* pfnCmdCopyImageToBuffer2_ = nullptr;
    void* pfnCmdBlitImage2_ = nullptr;
    void* pfnCmdResolveImage2_ = nullptr;
    void* pfnQueueSubmit2_ = nullptr;
    void* pfnCmdWaitEvents2_ = nullptr;
    void* pfnCmdSetVertexInputEXT_ = nullptr;
    void* pfnBindBufferMemory2_ = nullptr;
    void* pfnBindImageMemory2_ = nullptr;
    void* pfnGetSemaphoreCounterValue_ = nullptr;

    void cache_device_procs(VkDevice dev);

    void teardown_framebuffer();
    bool copy_image_to_readback();
};

} // namespace omnigpu::host
