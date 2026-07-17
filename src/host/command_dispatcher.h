#pragma once

#include "vulkan_deserializer.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <memory>
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

    void store_instance(uint64_t g, VkInstance v) { instances_[g] = v; }
    void store_device(uint64_t g, VkDevice v) { devices_[g] = v; }
    void store_queue(uint64_t g, VkQueue v) { queues_[g] = v; }
    void store_command_pool(uint64_t g, VkCommandPool v) { cmdPools_[g] = v; }
    void store_command_buffer(uint64_t g, VkCommandBuffer v) { cmdBufs_[g] = v; }
    void store_buffer(uint64_t g, VkBuffer v) { buffers_[g] = v; }
    void store_image(uint64_t g, VkImage v) { images_[g] = v; }
    void store_image_view(uint64_t g, VkImageView v) { imageViews_[g] = v; }
    void store_view_image(uint64_t gView, VkImage hostImg) { viewImages_[gView] = hostImg; }
    VkImage get_view_image(uint64_t gView) const { return lookup(viewImages_, gView); }
    void store_sampler(uint64_t g, VkSampler v) { samplers_[g] = v; }
    void store_shader_module(uint64_t g, VkShaderModule v) { shaderModules_[g] = v; }
    void store_pipeline_layout(uint64_t g, VkPipelineLayout v) { pipelineLayouts_[g] = v; }
    void store_pipeline(uint64_t g, VkPipeline v) { pipelines_[g] = v; }
    void store_pipeline_cache(uint64_t g, VkPipelineCache v) { pipelineCaches_[g] = v; }
    void store_render_pass(uint64_t g, VkRenderPass v) { renderPasses_[g] = v; }
    void store_framebuffer(uint64_t g, VkFramebuffer v) { framebuffers_[g] = v; }
    void store_descriptor_set_layout(uint64_t g, VkDescriptorSetLayout v) { dsls_[g] = v; }
    void store_descriptor_pool(uint64_t g, VkDescriptorPool v) { dps_[g] = v; }
    void store_descriptor_set(uint64_t g, VkDescriptorSet v) { dss_[g] = v; }
    void store_fence(uint64_t g, VkFence v) { fences_[g] = v; }
    void store_semaphore(uint64_t g, VkSemaphore v) { semaphores_[g] = v; }
    void store_event(uint64_t g, VkEvent v) { events_[g] = v; }
    void store_query_pool(uint64_t g, VkQueryPool v) { queryPools_[g] = v; }
    void store_private_data_slot(uint64_t g, VkPrivateDataSlot v) { privateDataSlots_[g] = v; }
    void store_device_memory(uint64_t g, VkDeviceMemory v) { memories_[g] = v; }
    void store_descriptor_update_template(uint64_t g, VkDescriptorUpdateTemplate v) { duts_[g] = v; }

    VkCommandPool    get_command_pool(uint64_t g) const { return lookup(cmdPools_, g); }
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
    void set_framebuffer_size(uint32_t w, uint32_t h);

    void dispatch(fbs::FunctionId func_id, const uint8_t* args, size_t args_size);
    bool flush_and_readback(std::vector<uint8_t>& out_pixels);

    bool begin_render_pass(VkRenderPass rp, VkFramebuffer fb,
                           uint32_t w, uint32_t h);
    void end_render_pass();

    bool setup_framebuffer();

    void cleanup();

    ResourceMapper& mapper() { return mapper_; }

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
    bool hasPendingSubmit_ = false;

    void teardown_framebuffer();
    bool copy_image_to_readback();
};

} // namespace omnigpu::host
