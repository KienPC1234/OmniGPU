#include "vulkan_struct_deserializer.h"
#include "../guest/vulkan_serializer.h"
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu::host {

static bool is_present(VulkanDeserializer& d) {
    return d.read_bool() != VK_FALSE;
}

bool read_VkSpecializationInfo(VulkanDeserializer& d, VkSpecializationInfo* out) {
    std::memset(out, 0, sizeof(*out));
    if (!is_present(d)) return true;
    uint32_t count = d.read_u32();
    out->mapEntryCount = count;
    if (count > 0) {
        out->pMapEntries = new VkSpecializationMapEntry[count];
        d.read_raw(const_cast<VkSpecializationMapEntry*>(out->pMapEntries),
                   count * sizeof(VkSpecializationMapEntry));
    }
    out->dataSize = static_cast<size_t>(d.read_u64());
    if (out->dataSize > 0) {
        out->pData = std::malloc(static_cast<size_t>(out->dataSize));
        d.read_raw(const_cast<void*>(out->pData), static_cast<size_t>(out->dataSize));
    }
    return d.ok();
}

bool read_VkPipelineShaderStageCreateInfo(VulkanDeserializer& d, VkPipelineShaderStageCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->stage = static_cast<VkShaderStageFlagBits>(d.read_u32());
    out->module = handle_from_u64<VkShaderModule>(d.read_handle());
    auto nameStr = d.read_string();
    // Heap-allocate a per-stage copy so multiple stages don't share the same pointer
    char* nameCopy = new char[nameStr.size() + 1];
    std::memcpy(nameCopy, nameStr.c_str(), nameStr.size() + 1);
    out->pName = nameCopy;
    // Allocate SpecializationInfo on heap (read_VkSpecializationInfo writes to it)
    auto* spec = new VkSpecializationInfo();
    read_VkSpecializationInfo(d, spec);
    out->pSpecializationInfo = spec;
    return d.ok();
}

bool read_VkComputePipelineCreateInfo(VulkanDeserializer& d, VkComputePipelineCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    out->flags = d.read_u32();
    read_VkPipelineShaderStageCreateInfo(d, &out->stage);
    out->layout = handle_from_u64<VkPipelineLayout>(d.read_handle());
    out->basePipelineHandle = handle_from_u64<VkPipeline>(d.read_handle());
    out->basePipelineIndex = d.read_i32();
    return d.ok();
}

void free_VkComputePipelineCreateInfo(VkComputePipelineCreateInfo* info) {
    if (!info) return;
    delete[] const_cast<char*>(info->stage.pName);
    auto* spec = const_cast<VkSpecializationInfo*>(info->stage.pSpecializationInfo);
    if (spec) {
        delete[] spec->pMapEntries;
        std::free(const_cast<void*>(spec->pData));
        delete spec;
    }
    std::memset(info, 0, sizeof(*info));
}

bool read_VkWriteDescriptorSet(VulkanDeserializer& d, VkWriteDescriptorSet* out,
                                VkDescriptorImageInfo** outImg, VkDescriptorBufferInfo** outBuf,
                                VkBufferView** outView) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    if (!is_present(d)) return false;
    out->dstSet = handle_from_u64<VkDescriptorSet>(d.read_handle());
    out->dstBinding = d.read_u32();
    out->descriptorCount = d.read_u32();
    out->descriptorType = static_cast<VkDescriptorType>(d.read_u32());
    uint32_t count = out->descriptorCount;
    if (count > 0) {
        *outImg = new VkDescriptorImageInfo[count];
        for (uint32_t i = 0; i < count; i++) {
            (*outImg)[i].sampler = handle_from_u64<VkSampler>(d.read_handle());
            (*outImg)[i].imageView = handle_from_u64<VkImageView>(d.read_handle());
            (*outImg)[i].imageLayout = static_cast<VkImageLayout>(d.read_u32());
        }
        *outBuf = new VkDescriptorBufferInfo[count];
        for (uint32_t i = 0; i < count; i++) {
            (*outBuf)[i].buffer = handle_from_u64<VkBuffer>(d.read_handle());
            (*outBuf)[i].offset = d.read_u64();
            (*outBuf)[i].range = d.read_u64();
        }
        *outView = new VkBufferView[count];
        for (uint32_t i = 0; i < count; i++)
            (*outView)[i] = handle_from_u64<VkBufferView>(d.read_handle());
    }
    out->pImageInfo = *outImg;
    out->pBufferInfo = *outBuf;
    out->pTexelBufferView = *outView;
    out->dstArrayElement = d.read_u32();
    return d.ok();
}

bool read_VkCommandPoolCreateInfo(VulkanDeserializer& d, VkCommandPoolCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    out->flags = static_cast<VkCommandPoolCreateFlags>(d.read_u32());
    out->queueFamilyIndex = d.read_u32();
    return d.ok();
}

bool read_VkBufferCreateInfo(VulkanDeserializer& d, VkBufferCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    out->flags = static_cast<VkBufferCreateFlags>(d.read_u32());
    out->size = static_cast<VkDeviceSize>(d.read_u64());
    out->usage = static_cast<VkBufferUsageFlags>(d.read_u32());
    out->sharingMode = static_cast<VkSharingMode>(d.read_u32());
    uint32_t qfi_count = d.read_u32();
    if (qfi_count > 0) {
        out->pQueueFamilyIndices = new uint32_t[qfi_count];
        d.read_raw(const_cast<uint32_t*>(out->pQueueFamilyIndices), qfi_count * sizeof(uint32_t));
        out->queueFamilyIndexCount = qfi_count;
    }
    return d.ok();
}

bool read_VkImageCreateInfo(VulkanDeserializer& d, VkImageCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    out->flags = static_cast<VkImageCreateFlags>(d.read_u32());
    out->imageType = static_cast<VkImageType>(d.read_u32());
    out->format = static_cast<VkFormat>(d.read_u32());
    d.read_raw(&out->extent, sizeof(VkExtent3D));
    out->mipLevels = d.read_u32();
    out->arrayLayers = d.read_u32();
    out->samples = static_cast<VkSampleCountFlagBits>(d.read_u32());
    out->tiling = static_cast<VkImageTiling>(d.read_u32());
    out->usage = static_cast<VkImageUsageFlags>(d.read_u32());
    out->sharingMode = static_cast<VkSharingMode>(d.read_u32());
    uint32_t qfi_count = d.read_u32();
    if (qfi_count > 0) {
        out->pQueueFamilyIndices = new uint32_t[qfi_count];
        d.read_raw(const_cast<uint32_t*>(out->pQueueFamilyIndices), qfi_count * sizeof(uint32_t));
        out->queueFamilyIndexCount = qfi_count;
    }
    out->initialLayout = static_cast<VkImageLayout>(d.read_u32());
    return d.ok();
}

bool read_VkImageViewCreateInfo(VulkanDeserializer& d, VkImageViewCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    out->flags = static_cast<VkImageViewCreateFlags>(d.read_u32());
    out->image = handle_from_u64<VkImage>(d.read_handle());
    out->viewType = static_cast<VkImageViewType>(d.read_u32());
    out->format = static_cast<VkFormat>(d.read_u32());
    d.read_raw(&out->components, sizeof(VkComponentMapping));
    d.read_raw(&out->subresourceRange, sizeof(VkImageSubresourceRange));
    return d.ok();
}

bool read_VkSamplerCreateInfo(VulkanDeserializer& d, VkSamplerCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    out->flags = static_cast<VkSamplerCreateFlags>(d.read_u32());
    out->magFilter = static_cast<VkFilter>(d.read_u32());
    out->minFilter = static_cast<VkFilter>(d.read_u32());
    out->mipmapMode = static_cast<VkSamplerMipmapMode>(d.read_u32());
    out->addressModeU = static_cast<VkSamplerAddressMode>(d.read_u32());
    out->addressModeV = static_cast<VkSamplerAddressMode>(d.read_u32());
    out->addressModeW = static_cast<VkSamplerAddressMode>(d.read_u32());
    out->mipLodBias = d.read_f32();
    out->anisotropyEnable = d.read_bool();
    out->maxAnisotropy = d.read_f32();
    out->compareEnable = d.read_bool();
    out->compareOp = static_cast<VkCompareOp>(d.read_u32());
    out->minLod = d.read_f32();
    out->maxLod = d.read_f32();
    out->borderColor = static_cast<VkBorderColor>(d.read_u32());
    out->unnormalizedCoordinates = d.read_bool();
    return d.ok();
}

bool read_VkPipelineLayoutCreateInfo(VulkanDeserializer& d, VkPipelineLayoutCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    out->flags = static_cast<VkPipelineLayoutCreateFlags>(d.read_u32());
    out->setLayoutCount = d.read_u32();
    if (out->setLayoutCount > 0) {
        out->pSetLayouts = new VkDescriptorSetLayout[out->setLayoutCount];
        for (uint32_t i = 0; i < out->setLayoutCount; i++)
            const_cast<VkDescriptorSetLayout*>(out->pSetLayouts)[i] = handle_from_u64<VkDescriptorSetLayout>(d.read_handle());
    }
    out->pushConstantRangeCount = d.read_u32();
    if (out->pushConstantRangeCount > 0) {
        out->pPushConstantRanges = new VkPushConstantRange[out->pushConstantRangeCount];
        d.read_raw(const_cast<VkPushConstantRange*>(out->pPushConstantRanges),
                   out->pushConstantRangeCount * sizeof(VkPushConstantRange));
    }
    return d.ok();
}

void free_VkPipelineLayoutCreateInfo(VkPipelineLayoutCreateInfo* info) {
    if (!info) return;
    delete[] info->pSetLayouts;
    delete[] info->pPushConstantRanges;
    std::memset(info, 0, sizeof(*info));
}

bool read_VkDescriptorSetLayoutCreateInfo(VulkanDeserializer& d, VkDescriptorSetLayoutCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    out->flags = static_cast<VkDescriptorSetLayoutCreateFlags>(d.read_u32());
    out->bindingCount = d.read_u32();
    if (out->bindingCount > 0) {
        out->pBindings = new VkDescriptorSetLayoutBinding[out->bindingCount]();
        for (uint32_t i = 0; i < out->bindingCount; i++) {
            auto* b = const_cast<VkDescriptorSetLayoutBinding*>(&out->pBindings[i]);
            b->binding = d.read_u32();
            b->descriptorType = static_cast<VkDescriptorType>(d.read_u32());
            b->descriptorCount = d.read_u32();
            b->stageFlags = static_cast<VkShaderStageFlags>(d.read_u32());
            if (d.read_bool()) {
                b->pImmutableSamplers = new VkSampler[b->descriptorCount];
                // Read as handles
                for (uint32_t j = 0; j < b->descriptorCount; j++)
                    const_cast<VkSampler*>(b->pImmutableSamplers)[j] = handle_from_u64<VkSampler>(d.read_handle());
            } else {
                b->pImmutableSamplers = nullptr;
            }
        }
    }
    return d.ok();
}

void free_VkDescriptorSetLayoutCreateInfo(VkDescriptorSetLayoutCreateInfo* info) {
    if (!info) return;
    for (uint32_t i = 0; i < info->bindingCount; i++)
        delete[] info->pBindings[i].pImmutableSamplers;
    delete[] info->pBindings;
    std::memset(info, 0, sizeof(*info));
}

bool read_VkDescriptorPoolCreateInfo(VulkanDeserializer& d, VkDescriptorPoolCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    out->flags = static_cast<VkDescriptorPoolCreateFlags>(d.read_u32());
    out->maxSets = d.read_u32();
    out->poolSizeCount = d.read_u32();
    if (out->poolSizeCount > 0) {
        out->pPoolSizes = new VkDescriptorPoolSize[out->poolSizeCount];
        for (uint32_t i = 0; i < out->poolSizeCount; i++) {
            auto* ps = const_cast<VkDescriptorPoolSize*>(&out->pPoolSizes[i]);
            ps->type = static_cast<VkDescriptorType>(d.read_u32());
            ps->descriptorCount = d.read_u32();
        }
    }
    return d.ok();
}

bool read_VkDescriptorSetAllocateInfo(VulkanDeserializer& d, VkDescriptorSetAllocateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    out->descriptorPool = handle_from_u64<VkDescriptorPool>(d.read_handle());
    out->descriptorSetCount = d.read_u32();
    if (out->descriptorSetCount > 0) {
        out->pSetLayouts = new VkDescriptorSetLayout[out->descriptorSetCount];
        for (uint32_t i = 0; i < out->descriptorSetCount; i++)
            const_cast<VkDescriptorSetLayout*>(out->pSetLayouts)[i] = handle_from_u64<VkDescriptorSetLayout>(d.read_handle());
    }
    return d.ok();
}

bool read_VkSemaphoreWaitInfo(VulkanDeserializer& d, VkSemaphoreWaitInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    out->flags = static_cast<VkSemaphoreWaitFlags>(d.read_u32());
    out->semaphoreCount = d.read_u32();
    if (out->semaphoreCount > 0) {
        out->pSemaphores = new VkSemaphore[out->semaphoreCount];
        out->pValues = new uint64_t[out->semaphoreCount];
        for (uint32_t i = 0; i < out->semaphoreCount; i++)
            const_cast<VkSemaphore*>(out->pSemaphores)[i] = handle_from_u64<VkSemaphore>(d.read_handle());
        for (uint32_t i = 0; i < out->semaphoreCount; i++)
            const_cast<uint64_t*>(out->pValues)[i] = d.read_u64();
    }
    return d.ok();
}

void free_VkSemaphoreWaitInfo(VkSemaphoreWaitInfo* info) {
    if (!info) return;
    delete[] info->pSemaphores;
    delete[] info->pValues;
    std::memset(info, 0, sizeof(*info));
}

bool read_VkSemaphoreSignalInfo(VulkanDeserializer& d, VkSemaphoreSignalInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    out->semaphore = handle_from_u64<VkSemaphore>(d.read_handle());
    out->value = d.read_u64();
    return d.ok();
}

bool read_VkMemoryAllocateInfo(VulkanDeserializer& d, VkMemoryAllocateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    out->allocationSize = static_cast<VkDeviceSize>(d.read_u64());
    out->memoryTypeIndex = d.read_u32();

    // Read pNext: VkMemoryDedicatedAllocateInfo
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    bool hasDedicated = d.read_bool() != VK_FALSE;
    if (hasDedicated) {
        dedicated.image = handle_from_u64<VkImage>(d.read_handle());
        dedicated.buffer = handle_from_u64<VkBuffer>(d.read_handle());
        dedicated.pNext = out->pNext;
        out->pNext = new VkMemoryDedicatedAllocateInfo(dedicated);
    }

    // Read pNext: VkMemoryAllocateFlagsInfo
    VkMemoryAllocateFlagsInfo flags{};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    bool hasFlags = d.read_bool() != VK_FALSE;
    if (hasFlags) {
        flags.flags = static_cast<VkMemoryAllocateFlags>(d.read_u64());
        flags.pNext = out->pNext;
        out->pNext = new VkMemoryAllocateFlagsInfo(flags);
    }

    return d.ok();
}

bool read_VkSubmitInfo(VulkanDeserializer& d, VkSubmitInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    out->waitSemaphoreCount = d.read_u32();
    if (out->waitSemaphoreCount > 0) {
        out->pWaitSemaphores = new VkSemaphore[out->waitSemaphoreCount];
        out->pWaitDstStageMask = new VkPipelineStageFlags[out->waitSemaphoreCount];
        for (uint32_t i = 0; i < out->waitSemaphoreCount; i++) {
            const_cast<VkSemaphore*>(out->pWaitSemaphores)[i] = handle_from_u64<VkSemaphore>(d.read_handle());
            const_cast<VkPipelineStageFlags*>(out->pWaitDstStageMask)[i] = static_cast<VkPipelineStageFlags>(d.read_u64());
        }
    }
    out->commandBufferCount = d.read_u32();
    if (out->commandBufferCount > 0) {
        out->pCommandBuffers = new VkCommandBuffer[out->commandBufferCount];
        for (uint32_t i = 0; i < out->commandBufferCount; i++)
            const_cast<VkCommandBuffer*>(out->pCommandBuffers)[i] = handle_from_u64<VkCommandBuffer>(d.read_handle());
    }
    out->signalSemaphoreCount = d.read_u32();
    if (out->signalSemaphoreCount > 0) {
        out->pSignalSemaphores = new VkSemaphore[out->signalSemaphoreCount];
        for (uint32_t i = 0; i < out->signalSemaphoreCount; i++)
            const_cast<VkSemaphore*>(out->pSignalSemaphores)[i] = handle_from_u64<VkSemaphore>(d.read_handle());
    }
    return d.ok();
}

void free_VkSubmitInfo(VkSubmitInfo* info) {
    if (!info) return;
    delete[] info->pWaitSemaphores;
    delete[] info->pWaitDstStageMask;
    delete[] info->pCommandBuffers;
    delete[] info->pSignalSemaphores;
    std::memset(info, 0, sizeof(*info));
}

bool read_VkSubmitInfo2(VulkanDeserializer& d, VkSubmitInfo2* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    out->flags = static_cast<VkSubmitFlags>(d.read_u32());
    out->waitSemaphoreInfoCount = d.read_u32();
    if (out->waitSemaphoreInfoCount > 0) {
        out->pWaitSemaphoreInfos = new VkSemaphoreSubmitInfo[out->waitSemaphoreInfoCount];
        d.read_raw(const_cast<VkSemaphoreSubmitInfo*>(out->pWaitSemaphoreInfos),
                   out->waitSemaphoreInfoCount * sizeof(VkSemaphoreSubmitInfo));
    }
    out->commandBufferInfoCount = d.read_u32();
    if (out->commandBufferInfoCount > 0) {
        out->pCommandBufferInfos = new VkCommandBufferSubmitInfo[out->commandBufferInfoCount];
        d.read_raw(const_cast<VkCommandBufferSubmitInfo*>(out->pCommandBufferInfos),
                   out->commandBufferInfoCount * sizeof(VkCommandBufferSubmitInfo));
    }
    out->signalSemaphoreInfoCount = d.read_u32();
    if (out->signalSemaphoreInfoCount > 0) {
        out->pSignalSemaphoreInfos = new VkSemaphoreSubmitInfo[out->signalSemaphoreInfoCount];
        d.read_raw(const_cast<VkSemaphoreSubmitInfo*>(out->pSignalSemaphoreInfos),
                   out->signalSemaphoreInfoCount * sizeof(VkSemaphoreSubmitInfo));
    }
    return d.ok();
}

void free_VkSubmitInfo2(VkSubmitInfo2* info) {
    if (!info) return;
    delete[] info->pWaitSemaphoreInfos;
    delete[] info->pCommandBufferInfos;
    delete[] info->pSignalSemaphoreInfos;
    std::memset(info, 0, sizeof(*info));
}

bool read_VkDependencyInfo(VulkanDeserializer& d, VkDependencyInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    out->dependencyFlags = static_cast<VkDependencyFlags>(d.read_u32());

    out->memoryBarrierCount = d.read_u32();
    if (out->memoryBarrierCount > 0) {
        auto* barriers = new VkMemoryBarrier2[out->memoryBarrierCount];
        for (uint32_t i = 0; i < out->memoryBarrierCount; i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barriers[i].pNext = nullptr;
            barriers[i].srcStageMask = d.read_u64();
            barriers[i].srcAccessMask = d.read_u64();
            barriers[i].dstStageMask = d.read_u64();
            barriers[i].dstAccessMask = d.read_u64();
        }
        out->pMemoryBarriers = barriers;
    }

    out->bufferMemoryBarrierCount = d.read_u32();
    if (out->bufferMemoryBarrierCount > 0) {
        auto* barriers = new VkBufferMemoryBarrier2[out->bufferMemoryBarrierCount];
        for (uint32_t i = 0; i < out->bufferMemoryBarrierCount; i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barriers[i].pNext = nullptr;
            barriers[i].srcStageMask = d.read_u64();
            barriers[i].srcAccessMask = d.read_u64();
            barriers[i].dstStageMask = d.read_u64();
            barriers[i].dstAccessMask = d.read_u64();
            barriers[i].srcQueueFamilyIndex = d.read_u32();
            barriers[i].dstQueueFamilyIndex = d.read_u32();
            barriers[i].buffer = handle_from_u64<VkBuffer>(d.read_handle());
            barriers[i].offset = d.read_u64();
            barriers[i].size = d.read_u64();
        }
        out->pBufferMemoryBarriers = barriers;
    }

    out->imageMemoryBarrierCount = d.read_u32();
    if (out->imageMemoryBarrierCount > 0) {
        auto* barriers = new VkImageMemoryBarrier2[out->imageMemoryBarrierCount];
        for (uint32_t i = 0; i < out->imageMemoryBarrierCount; i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barriers[i].pNext = nullptr;
            barriers[i].srcStageMask = d.read_u64();
            barriers[i].srcAccessMask = d.read_u64();
            barriers[i].dstStageMask = d.read_u64();
            barriers[i].dstAccessMask = d.read_u64();
            barriers[i].oldLayout = static_cast<VkImageLayout>(d.read_u32());
            barriers[i].newLayout = static_cast<VkImageLayout>(d.read_u32());
            barriers[i].srcQueueFamilyIndex = d.read_u32();
            barriers[i].dstQueueFamilyIndex = d.read_u32();
            barriers[i].image = handle_from_u64<VkImage>(d.read_handle());
            d.read_raw(&barriers[i].subresourceRange, sizeof(VkImageSubresourceRange));
        }
        out->pImageMemoryBarriers = barriers;
    }

    return d.ok();
}

void free_VkDependencyInfo(VkDependencyInfo* info) {
    if (!info) return;
    delete[] static_cast<const VkMemoryBarrier2*>(info->pMemoryBarriers);
    delete[] static_cast<const VkBufferMemoryBarrier2*>(info->pBufferMemoryBarriers);
    delete[] static_cast<const VkImageMemoryBarrier2*>(info->pImageMemoryBarriers);
    std::memset(info, 0, sizeof(*info));
}

} // namespace omnigpu::host
