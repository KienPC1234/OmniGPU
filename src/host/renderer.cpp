#include "renderer.h"
#include <cstring>
#include <spdlog/spdlog.h>
#include <vector>

namespace omnigpu {

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(VkPhysicalDevice physDevice, uint32_t width, uint32_t height) {
    physDevice_ = physDevice;
    width_ = width;
    height_ = height;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDevice_, &props);
    SPDLOG_INFO("Renderer initializing on GPU: {} ({}x{})", props.deviceName, width_, height_);

    if (!create_device()) return false;
    if (!create_render_target()) return false;
    if (!create_readback_buffer()) return false;

    return true;
}

void Renderer::shutdown() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);

    if (frameCmd_) {
        vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
        frameCmd_ = VK_NULL_HANDLE;
    }
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    if (readbackMemory_) vkFreeMemory(device_, readbackMemory_, nullptr);
    if (readbackBuffer_) vkDestroyBuffer(device_, readbackBuffer_, nullptr);
    if (framebuffer_) vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    if (colorView_) vkDestroyImageView(device_, colorView_, nullptr);
    if (colorMemory_) vkFreeMemory(device_, colorMemory_, nullptr);
    if (colorImage_) vkDestroyImage(device_, colorImage_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);

    vkDestroyDevice(device_, nullptr);

    device_ = VK_NULL_HANDLE;
    physDevice_ = VK_NULL_HANDLE;
}

bool Renderer::create_device() {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice_, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice_, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily_ = i;
            break;
        }
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.pEnabledFeatures = &features;

    if (vkCreateDevice(physDevice_, &info, nullptr, &device_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create Vulkan device");
        return false;
    }

    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool Renderer::create_render_target() {
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = {width_, height_, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &imgInfo, nullptr, &colorImage_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create color image");
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, colorImage_, &memReq);

    VkPhysicalDeviceMemoryProperties physMem;
    vkGetPhysicalDeviceMemoryProperties(physDevice_, &physMem);

    uint32_t memType = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < physMem.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (physMem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memType = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &colorMemory_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to allocate color image memory");
        return false;
    }

    vkBindImageMemory(device_, colorImage_, colorMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &colorView_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create image view");
        return false;
    }

    VkAttachmentDescription colorAtt{};
    colorAtt.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAtt;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create render pass");
        return false;
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &colorView_;
    fbInfo.width = width_;
    fbInfo.height = height_;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create framebuffer");
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &cmdPool_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create command pool");
        return false;
    }

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = cmdPool_;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device_, &cmdAlloc, &setupCmd_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to allocate setup command buffer");
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(setupCmd_, &beginInfo);
    record_transition_layout(setupCmd_, colorImage_,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkEndCommandBuffer(setupCmd_);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &setupCmd_;

    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    return true;
}

bool Renderer::create_readback_buffer() {
    VkDeviceSize size = static_cast<VkDeviceSize>(width_) * height_ * 4;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufInfo, nullptr, &readbackBuffer_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create readback buffer");
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, readbackBuffer_, &memReq);

    VkPhysicalDeviceMemoryProperties physMem;
    vkGetPhysicalDeviceMemoryProperties(physDevice_, &physMem);

    uint32_t memType = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < physMem.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (physMem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (physMem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memType = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memType;

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &readbackMemory_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to allocate readback memory");
        return false;
    }

    vkBindBufferMemory(device_, readbackBuffer_, readbackMemory_, 0);
    return true;
}

void Renderer::record_transition_layout(VkCommandBuffer cmd, VkImage image,
                                         VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

VkCommandBuffer Renderer::begin_frame() {
    // Free previous command buffer if not yet consumed
    if (frameCmd_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
        frameCmd_ = VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device_, &allocInfo, &frameCmd_) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(frameCmd_, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
        frameCmd_ = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }

    VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = framebuffer_;
    rpBegin.renderArea.extent = {width_, height_};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clear;

    vkCmdBeginRenderPass(frameCmd_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    return frameCmd_;
}

bool Renderer::submit_and_readback(std::vector<uint8_t>& out_pixels) {
    if (frameCmd_ == VK_NULL_HANDLE) return false;

    vkCmdEndRenderPass(frameCmd_);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = colorImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(frameCmd_,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width_, height_, 1};

    vkCmdCopyImageToBuffer(frameCmd_, colorImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer_, 1, &copy);

    if (vkEndCommandBuffer(frameCmd_) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
        frameCmd_ = VK_NULL_HANDLE;
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
        frameCmd_ = VK_NULL_HANDLE;
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frameCmd_;

    if (vkQueueSubmit(queue_, 1, &submit, fence) != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
        frameCmd_ = VK_NULL_HANDLE;
        return false;
    }

    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &frameCmd_);
    frameCmd_ = VK_NULL_HANDLE;

    VkDeviceSize size = static_cast<VkDeviceSize>(width_) * height_ * 4;
    void* mapped;
    if (vkMapMemory(device_, readbackMemory_, 0, size, 0, &mapped) != VK_SUCCESS) {
        return false;
    }

    out_pixels.resize(static_cast<size_t>(size));
    std::memcpy(out_pixels.data(), mapped, static_cast<size_t>(size));
    vkUnmapMemory(device_, readbackMemory_);

    return true;
}

} // namespace omnigpu
