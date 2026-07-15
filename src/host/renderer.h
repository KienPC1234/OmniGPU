#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace omnigpu {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    bool init(VkPhysicalDevice physDevice, uint32_t width, uint32_t height);
    void shutdown();

    VkCommandBuffer begin_frame();
    bool submit_and_readback(std::vector<uint8_t>& out_pixels);

    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queue_family_index() const { return queueFamily_; }
    VkRenderPass render_pass() const { return renderPass_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    VkExtent2D extent() const { return {width_, height_}; }
    VkCommandPool command_pool() const { return cmdPool_; }
    VkPhysicalDevice physical_device() const { return physDevice_; }

private:
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;

    uint32_t width_ = 800;
    uint32_t height_ = 600;

    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkCommandBuffer frameCmd_ = VK_NULL_HANDLE;
    VkCommandBuffer setupCmd_ = VK_NULL_HANDLE;
    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;

    bool create_device();
    bool create_render_target();
    bool create_readback_buffer();
    bool create_semaphore();
    void record_transition_layout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout);
};

} // namespace omnigpu
