#include "vulkan_struct_serializer.h"

namespace omnigpu::serializer {

// ── helpers ──────────────────────────────────────────────────────────
static void write_nullable_ptr(VulkanSerializer& ser, const void* ptr) {
    ser.write_bool(ptr != nullptr);
}

template<typename T>
static void write_array_t(VulkanSerializer& ser, const T* arr, uint32_t count) {
    ser.write_u32(count);
    for (uint32_t i = 0; i < count; i++)
        ser.write_raw(&arr[i], sizeof(T));
}

// ── Shader Module (has pCode data pointer) ────────────────────────────

void write_VkShaderModuleCreateInfo(VulkanSerializer& ser, const VkShaderModuleCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u64(static_cast<uint64_t>(info->codeSize));
    if (info->pCode && info->codeSize > 0)
        ser.write_raw(info->pCode, static_cast<size_t>(info->codeSize));
}

// ── Semaphore (handles pNext → VkSemaphoreTypeCreateInfo for timeline) ─

void write_VkSemaphoreCreateInfo(VulkanSerializer& ser, const VkSemaphoreCreateInfo* info) {
    ser.write_u32(info->flags);
    bool has_timeline = false;
    uint32_t sem_type = 0;
    uint64_t initial_val = 0;
    if (info->pNext) {
        auto* ext = static_cast<const VkBaseInStructure*>(info->pNext);
        while (ext) {
            if (ext->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
                auto* ti = reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(ext);
                has_timeline = true;
                sem_type = static_cast<uint32_t>(ti->semaphoreType);
                initial_val = ti->initialValue;
                break;
            }
            ext = ext->pNext;
        }
    }
    ser.write_bool(has_timeline ? VK_TRUE : VK_FALSE);
    if (has_timeline) {
        ser.write_u32(sem_type);
        ser.write_u64(initial_val);
    }
}

// ── Descriptor Update Template (has pDescriptorUpdateEntries array) ───

void write_VkDescriptorUpdateTemplateCreateInfo(
    VulkanSerializer& ser, const VkDescriptorUpdateTemplateCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->descriptorUpdateEntryCount);
    for (uint32_t i = 0; i < info->descriptorUpdateEntryCount; i++) {
        auto& e = info->pDescriptorUpdateEntries[i];
        ser.write_u32(e.dstBinding);
        ser.write_u32(e.dstArrayElement);
        ser.write_u32(e.descriptorCount);
        ser.write_u32(static_cast<uint32_t>(e.descriptorType));
        ser.write_u64(static_cast<uint64_t>(e.offset));
        ser.write_u64(static_cast<uint64_t>(e.stride));
    }
    ser.write_u32(static_cast<uint32_t>(info->templateType));
    ser.write_handle(handle_to_u64(info->descriptorSetLayout));
    ser.write_u32(static_cast<uint32_t>(info->pipelineBindPoint));
    ser.write_handle(handle_to_u64(info->pipelineLayout));
    ser.write_u32(info->set);
}

// ── sub-struct serializers ───────────────────────────────────────────

void write_VkSpecializationInfo(VulkanSerializer& ser, const VkSpecializationInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(static_cast<uint32_t>(info->mapEntryCount));
    for (uint32_t i = 0; i < info->mapEntryCount; i++)
        ser.write_raw(&info->pMapEntries[i], sizeof(VkSpecializationMapEntry));
    ser.write_u64(static_cast<uint64_t>(info->dataSize));
    if (info->pData && info->dataSize > 0)
        ser.write_raw(info->pData, static_cast<size_t>(info->dataSize));
}

void write_VkPipelineShaderStageCreateInfo(VulkanSerializer& ser, const VkPipelineShaderStageCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(static_cast<uint32_t>(info->stage));
    ser.write_handle(handle_to_u64(info->module));
    ser.write_string(info->pName ? info->pName : "main");
    write_VkSpecializationInfo(ser, info->pSpecializationInfo);
}

void write_VkPipelineVertexInputStateCreateInfo(VulkanSerializer& ser, const VkPipelineVertexInputStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(info->vertexBindingDescriptionCount);
    for (uint32_t i = 0; i < info->vertexBindingDescriptionCount; i++)
        ser.write_raw(&info->pVertexBindingDescriptions[i], sizeof(VkVertexInputBindingDescription));
    ser.write_u32(info->vertexAttributeDescriptionCount);
    for (uint32_t i = 0; i < info->vertexAttributeDescriptionCount; i++)
        ser.write_raw(&info->pVertexAttributeDescriptions[i], sizeof(VkVertexInputAttributeDescription));
}

void write_VkPipelineInputAssemblyStateCreateInfo(VulkanSerializer& ser, const VkPipelineInputAssemblyStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(static_cast<uint32_t>(info->topology));
    ser.write_bool(info->primitiveRestartEnable);
}

void write_VkPipelineTessellationStateCreateInfo(VulkanSerializer& ser, const VkPipelineTessellationStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(info->patchControlPoints);
}

void write_VkPipelineViewportStateCreateInfo(VulkanSerializer& ser, const VkPipelineViewportStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(info->viewportCount);
    // pViewports may be null when using dynamic viewport state
    ser.write_bool(info->pViewports != nullptr);
    if (info->pViewports) {
        for (uint32_t i = 0; i < info->viewportCount; i++)
            ser.write_raw(&info->pViewports[i], sizeof(VkViewport));
    }
    ser.write_u32(info->scissorCount);
    // pScissors may be null when using dynamic scissor state
    ser.write_bool(info->pScissors != nullptr);
    if (info->pScissors) {
        for (uint32_t i = 0; i < info->scissorCount; i++)
            ser.write_raw(&info->pScissors[i], sizeof(VkRect2D));
    }
}

void write_VkPipelineRasterizationStateCreateInfo(VulkanSerializer& ser, const VkPipelineRasterizationStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_bool(info->depthClampEnable);
    ser.write_bool(info->rasterizerDiscardEnable);
    ser.write_u32(static_cast<uint32_t>(info->polygonMode));
    ser.write_u32(info->cullMode);
    ser.write_u32(static_cast<uint32_t>(info->frontFace));
    ser.write_bool(info->depthBiasEnable);
    ser.write_f32(info->depthBiasConstantFactor);
    ser.write_f32(info->depthBiasClamp);
    ser.write_f32(info->depthBiasSlopeFactor);
    ser.write_f32(info->lineWidth);
}

void write_VkPipelineMultisampleStateCreateInfo(VulkanSerializer& ser, const VkPipelineMultisampleStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(static_cast<uint32_t>(info->rasterizationSamples));
    ser.write_bool(info->sampleShadingEnable);
    ser.write_f32(info->minSampleShading);
    // Always write presence flag so host can read consistently
    ser.write_bool(info->pSampleMask != nullptr);
    if (info->pSampleMask)
        ser.write_raw(info->pSampleMask, sizeof(uint32_t));
    ser.write_bool(info->alphaToCoverageEnable);
    ser.write_bool(info->alphaToOneEnable);
}

void write_VkPipelineDepthStencilStateCreateInfo(VulkanSerializer& ser, const VkPipelineDepthStencilStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_bool(info->depthTestEnable);
    ser.write_bool(info->depthWriteEnable);
    ser.write_u32(static_cast<uint32_t>(info->depthCompareOp));
    ser.write_bool(info->depthBoundsTestEnable);
    ser.write_bool(info->stencilTestEnable);
    ser.write_raw(&info->front, sizeof(VkStencilOpState));
    ser.write_raw(&info->back, sizeof(VkStencilOpState));
    ser.write_f32(info->minDepthBounds);
    ser.write_f32(info->maxDepthBounds);
}

void write_VkPipelineColorBlendAttachmentState(VulkanSerializer& ser, const VkPipelineColorBlendAttachmentState* state) {
    ser.write_bool(state->blendEnable);
    ser.write_u32(static_cast<uint32_t>(state->srcColorBlendFactor));
    ser.write_u32(static_cast<uint32_t>(state->dstColorBlendFactor));
    ser.write_u32(static_cast<uint32_t>(state->colorBlendOp));
    ser.write_u32(static_cast<uint32_t>(state->srcAlphaBlendFactor));
    ser.write_u32(static_cast<uint32_t>(state->dstAlphaBlendFactor));
    ser.write_u32(static_cast<uint32_t>(state->alphaBlendOp));
    ser.write_u32(state->colorWriteMask);
}

void write_VkPipelineColorBlendStateCreateInfo(VulkanSerializer& ser, const VkPipelineColorBlendStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_bool(info->logicOpEnable);
    ser.write_u32(static_cast<uint32_t>(info->logicOp));
    ser.write_u32(info->attachmentCount);
    for (uint32_t i = 0; i < info->attachmentCount; i++)
        write_VkPipelineColorBlendAttachmentState(ser, &info->pAttachments[i]);
    ser.write_raw(info->blendConstants, sizeof(float) * 4);
}

void write_VkPipelineDynamicStateCreateInfo(VulkanSerializer& ser, const VkPipelineDynamicStateCreateInfo* info) {
    if (!info) { write_nullable_ptr(ser, nullptr); return; }
    write_nullable_ptr(ser, info);
    ser.write_u32(info->flags);
    ser.write_u32(info->dynamicStateCount);
    for (uint32_t i = 0; i < info->dynamicStateCount; i++)
        ser.write_u32(static_cast<uint32_t>(info->pDynamicStates[i]));
}

void write_VkGraphicsPipelineCreateInfo(VulkanSerializer& ser, const VkGraphicsPipelineCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->stageCount);
    for (uint32_t i = 0; i < info->stageCount; i++)
        write_VkPipelineShaderStageCreateInfo(ser, &info->pStages[i]);
    write_VkPipelineVertexInputStateCreateInfo(ser, info->pVertexInputState);
    write_VkPipelineInputAssemblyStateCreateInfo(ser, info->pInputAssemblyState);
    write_VkPipelineTessellationStateCreateInfo(ser, info->pTessellationState);
    write_VkPipelineViewportStateCreateInfo(ser, info->pViewportState);
    write_VkPipelineRasterizationStateCreateInfo(ser, info->pRasterizationState);
    write_VkPipelineMultisampleStateCreateInfo(ser, info->pMultisampleState);
    write_VkPipelineDepthStencilStateCreateInfo(ser, info->pDepthStencilState);
    write_VkPipelineColorBlendStateCreateInfo(ser, info->pColorBlendState);
    write_VkPipelineDynamicStateCreateInfo(ser, info->pDynamicState);
    ser.write_handle(handle_to_u64(info->layout));
    ser.write_handle(handle_to_u64(info->renderPass));
    ser.write_u32(info->subpass);
    ser.write_handle(handle_to_u64(info->basePipelineHandle));
    ser.write_i32(info->basePipelineIndex);
}

void write_VkComputePipelineCreateInfo(VulkanSerializer& ser, const VkComputePipelineCreateInfo* info) {
    ser.write_u32(info->flags);
    write_VkPipelineShaderStageCreateInfo(ser, &info->stage);
    ser.write_handle(handle_to_u64(info->layout));
    ser.write_handle(handle_to_u64(info->basePipelineHandle));
    ser.write_i32(info->basePipelineIndex);
}

void write_VkDescriptorImageInfo(VulkanSerializer& ser, const VkDescriptorImageInfo* info) {
    ser.write_handle(handle_to_u64(info->sampler));
    ser.write_handle(handle_to_u64(info->imageView));
    ser.write_u32(static_cast<uint32_t>(info->imageLayout));
}

void write_VkDescriptorBufferInfo(VulkanSerializer& ser, const VkDescriptorBufferInfo* info) {
    ser.write_handle(handle_to_u64(info->buffer));
    ser.write_u64(static_cast<uint64_t>(info->offset));
    ser.write_u64(static_cast<uint64_t>(info->range));
}

void write_VkWriteDescriptorSet(VulkanSerializer& ser, const VkWriteDescriptorSet* info) {
    write_nullable_ptr(ser, info);
    ser.write_handle(handle_to_u64(info->dstSet));
    ser.write_u32(info->dstBinding);
    ser.write_u32(info->descriptorCount);
    ser.write_u32(static_cast<uint32_t>(info->descriptorType));
    for (uint32_t i = 0; i < info->descriptorCount; i++) {
        if (info->pImageInfo)
            write_VkDescriptorImageInfo(ser, &info->pImageInfo[i]);
        else {
            ser.write_handle(0); ser.write_handle(0); ser.write_u32(0);
        }
    }
    for (uint32_t i = 0; i < info->descriptorCount; i++) {
        if (info->pBufferInfo)
            write_VkDescriptorBufferInfo(ser, &info->pBufferInfo[i]);
        else {
            ser.write_handle(0); ser.write_u64(0); ser.write_u64(0);
        }
    }
    for (uint32_t i = 0; i < info->descriptorCount; i++)
        ser.write_handle(info->pTexelBufferView
            ? handle_to_u64(info->pTexelBufferView[i])
            : 0);
    ser.write_u32(info->dstArrayElement);
}

void write_VkRenderingAttachmentInfo(VulkanSerializer& ser, const VkRenderingAttachmentInfo* info) {
    ser.write_handle(handle_to_u64(info->imageView));
    ser.write_u32(static_cast<uint32_t>(info->imageLayout));
    ser.write_u32(static_cast<uint32_t>(info->resolveMode));
    ser.write_handle(handle_to_u64(info->resolveImageView));
    ser.write_u32(static_cast<uint32_t>(info->resolveImageLayout));
    ser.write_u32(static_cast<uint32_t>(info->loadOp));
    ser.write_u32(static_cast<uint32_t>(info->storeOp));
    ser.write_raw(&info->clearValue, sizeof(VkClearValue));
}

void write_VkRenderingInfo(VulkanSerializer& ser, const VkRenderingInfo* info) {
    ser.write_u32(info->flags);
    ser.write_raw(&info->renderArea, sizeof(VkRect2D));
    ser.write_u32(info->layerCount);
    ser.write_u32(info->viewMask);
    ser.write_u32(info->colorAttachmentCount);
    for (uint32_t i = 0; i < info->colorAttachmentCount; i++)
        write_VkRenderingAttachmentInfo(ser, &info->pColorAttachments[i]);
    if (info->pDepthAttachment) {
        write_nullable_ptr(ser, info->pDepthAttachment);
        write_VkRenderingAttachmentInfo(ser, info->pDepthAttachment);
    } else {
        write_nullable_ptr(ser, nullptr);
    }
    if (info->pStencilAttachment) {
        write_nullable_ptr(ser, info->pStencilAttachment);
        write_VkRenderingAttachmentInfo(ser, info->pStencilAttachment);
    } else {
        write_nullable_ptr(ser, nullptr);
    }
}

void write_VkSubmitInfo(VulkanSerializer& ser, const VkSubmitInfo* info) {
    ser.write_u32(info->waitSemaphoreCount);
    for (uint32_t i = 0; i < info->waitSemaphoreCount; i++) {
        ser.write_handle(handle_to_u64(info->pWaitSemaphores[i]));
        ser.write_u64(static_cast<uint64_t>(info->pWaitDstStageMask[i]));
    }
    ser.write_u32(info->commandBufferCount);
    for (uint32_t i = 0; i < info->commandBufferCount; i++)
        ser.write_handle(handle_to_u64(info->pCommandBuffers[i]));
    ser.write_u32(info->signalSemaphoreCount);
    for (uint32_t i = 0; i < info->signalSemaphoreCount; i++)
        ser.write_handle(handle_to_u64(info->pSignalSemaphores[i]));
}

void write_VkSemaphoreWaitInfo(VulkanSerializer& ser, const VkSemaphoreWaitInfo* info) {
    ser.write_u32(static_cast<uint32_t>(info->flags));
    ser.write_u32(info->semaphoreCount);
    for (uint32_t i = 0; i < info->semaphoreCount; i++)
        ser.write_handle(handle_to_u64(info->pSemaphores[i]));
    for (uint32_t i = 0; i < info->semaphoreCount; i++)
        ser.write_u64(info->pValues[i]);
}

void write_VkSemaphoreSignalInfo(VulkanSerializer& ser, const VkSemaphoreSignalInfo* info) {
    ser.write_handle(handle_to_u64(info->semaphore));
    ser.write_u64(info->value);
}

void write_VkSubmitInfo2(VulkanSerializer& ser, const VkSubmitInfo2* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->waitSemaphoreInfoCount);
    for (uint32_t i = 0; i < info->waitSemaphoreInfoCount; i++)
        ser.write_raw(&info->pWaitSemaphoreInfos[i], sizeof(VkSemaphoreSubmitInfo));
    ser.write_u32(info->commandBufferInfoCount);
    for (uint32_t i = 0; i < info->commandBufferInfoCount; i++)
        ser.write_raw(&info->pCommandBufferInfos[i], sizeof(VkCommandBufferSubmitInfo));
    ser.write_u32(info->signalSemaphoreInfoCount);
    for (uint32_t i = 0; i < info->signalSemaphoreInfoCount; i++)
        ser.write_raw(&info->pSignalSemaphoreInfos[i], sizeof(VkSemaphoreSubmitInfo));
}

void write_VkDependencyInfo(VulkanSerializer& ser, const VkDependencyInfo* info) {
    ser.write_u32(static_cast<uint32_t>(info->dependencyFlags));
    ser.write_u32(info->memoryBarrierCount);
    for (uint32_t i = 0; i < info->memoryBarrierCount; i++) {
        const auto& b = info->pMemoryBarriers[i];
        ser.write_u64(b.srcStageMask);
        ser.write_u64(b.srcAccessMask);
        ser.write_u64(b.dstStageMask);
        ser.write_u64(b.dstAccessMask);
    }
    ser.write_u32(info->bufferMemoryBarrierCount);
    for (uint32_t i = 0; i < info->bufferMemoryBarrierCount; i++) {
        const auto& b = info->pBufferMemoryBarriers[i];
        ser.write_u64(b.srcStageMask);
        ser.write_u64(b.srcAccessMask);
        ser.write_u64(b.dstStageMask);
        ser.write_u64(b.dstAccessMask);
        ser.write_u32(b.srcQueueFamilyIndex);
        ser.write_u32(b.dstQueueFamilyIndex);
        ser.write_handle(handle_to_u64(b.buffer));
        ser.write_u64(b.offset);
        ser.write_u64(b.size);
    }
    ser.write_u32(info->imageMemoryBarrierCount);
    for (uint32_t i = 0; i < info->imageMemoryBarrierCount; i++) {
        const auto& b = info->pImageMemoryBarriers[i];
        ser.write_u64(b.srcStageMask);
        ser.write_u64(b.srcAccessMask);
        ser.write_u64(b.dstStageMask);
        ser.write_u64(b.dstAccessMask);
        ser.write_u32(static_cast<uint32_t>(b.oldLayout));
        ser.write_u32(static_cast<uint32_t>(b.newLayout));
        ser.write_u32(b.srcQueueFamilyIndex);
        ser.write_u32(b.dstQueueFamilyIndex);
        ser.write_handle(handle_to_u64(b.image));
        ser.write_raw(&b.subresourceRange, sizeof(VkImageSubresourceRange));
    }
}

void write_VkRenderPassBeginInfo(VulkanSerializer& ser, const VkRenderPassBeginInfo* info) {
    ser.write_handle(handle_to_u64(info->renderPass));
    ser.write_handle(handle_to_u64(info->framebuffer));
    ser.write_raw(&info->renderArea, sizeof(VkRect2D));
    ser.write_u32(info->clearValueCount);
    for (uint32_t i = 0; i < info->clearValueCount; i++)
        ser.write_raw(&info->pClearValues[i], sizeof(VkClearValue));
}

// ── CommandPool ──────────────────────────────────────────────────
void write_VkCommandPoolCreateInfo(VulkanSerializer& ser, const VkCommandPoolCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->queueFamilyIndex);
}

// ── Buffer ────────────────────────────────────────────────────────
void write_VkBufferCreateInfo(VulkanSerializer& ser, const VkBufferCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u64(static_cast<uint64_t>(info->size));
    ser.write_u32(static_cast<uint32_t>(info->usage));
    ser.write_u32(static_cast<uint32_t>(info->sharingMode));
    write_array_t(ser, info->pQueueFamilyIndices, info->queueFamilyIndexCount);
}

// ── Image ─────────────────────────────────────────────────────────
void write_VkImageCreateInfo(VulkanSerializer& ser, const VkImageCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(static_cast<uint32_t>(info->imageType));
    ser.write_u32(static_cast<uint32_t>(info->format));
    ser.write_raw(&info->extent, sizeof(VkExtent3D));
    ser.write_u32(info->mipLevels);
    ser.write_u32(info->arrayLayers);
    ser.write_u32(static_cast<uint32_t>(info->samples));
    ser.write_u32(static_cast<uint32_t>(info->tiling));
    ser.write_u32(static_cast<uint32_t>(info->usage));
    ser.write_u32(static_cast<uint32_t>(info->sharingMode));
    write_array_t(ser, info->pQueueFamilyIndices, info->queueFamilyIndexCount);
    ser.write_u32(static_cast<uint32_t>(info->initialLayout));
}

// ── ImageView ─────────────────────────────────────────────────────
void write_VkImageViewCreateInfo(VulkanSerializer& ser, const VkImageViewCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_handle(handle_to_u64(info->image));
    ser.write_u32(static_cast<uint32_t>(info->viewType));
    ser.write_u32(static_cast<uint32_t>(info->format));
    ser.write_raw(&info->components, sizeof(VkComponentMapping));
    ser.write_raw(&info->subresourceRange, sizeof(VkImageSubresourceRange));
}

// ── Sampler ───────────────────────────────────────────────────────
void write_VkSamplerCreateInfo(VulkanSerializer& ser, const VkSamplerCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(static_cast<uint32_t>(info->magFilter));
    ser.write_u32(static_cast<uint32_t>(info->minFilter));
    ser.write_u32(static_cast<uint32_t>(info->mipmapMode));
    ser.write_u32(static_cast<uint32_t>(info->addressModeU));
    ser.write_u32(static_cast<uint32_t>(info->addressModeV));
    ser.write_u32(static_cast<uint32_t>(info->addressModeW));
    ser.write_f32(info->mipLodBias);
    ser.write_bool(info->anisotropyEnable);
    ser.write_f32(info->maxAnisotropy);
    ser.write_bool(info->compareEnable);
    ser.write_u32(static_cast<uint32_t>(info->compareOp));
    ser.write_f32(info->minLod);
    ser.write_f32(info->maxLod);
    ser.write_u32(static_cast<uint32_t>(info->borderColor));
    ser.write_bool(info->unnormalizedCoordinates);
}

// ── AttachmentDescription (sub-struct for RenderPass) ──────────────
static void write_VkAttachmentDescription(VulkanSerializer& ser, const VkAttachmentDescription* att) {
    ser.write_u32(att->flags);
    ser.write_u32(static_cast<uint32_t>(att->format));
    ser.write_u32(static_cast<uint32_t>(att->samples));
    ser.write_u32(static_cast<uint32_t>(att->loadOp));
    ser.write_u32(static_cast<uint32_t>(att->storeOp));
    ser.write_u32(static_cast<uint32_t>(att->stencilLoadOp));
    ser.write_u32(static_cast<uint32_t>(att->stencilStoreOp));
    ser.write_u32(static_cast<uint32_t>(att->initialLayout));
    ser.write_u32(static_cast<uint32_t>(att->finalLayout));
}

// ── AttachmentReference (sub-struct for SubpassDescription) ────────
static void write_VkAttachmentReference(VulkanSerializer& ser, const VkAttachmentReference* ref) {
    ser.write_u32(ref->attachment);
    ser.write_u32(static_cast<uint32_t>(ref->layout));
}

// ── SubpassDescription (has nested pointer arrays) ─────────────────
static void write_VkSubpassDescription(VulkanSerializer& ser, const VkSubpassDescription* sd) {
    ser.write_u32(static_cast<uint32_t>(sd->flags));
    ser.write_u32(static_cast<uint32_t>(sd->pipelineBindPoint));
    ser.write_u32(sd->inputAttachmentCount);
    for (uint32_t i = 0; i < sd->inputAttachmentCount; i++)
        write_VkAttachmentReference(ser, &sd->pInputAttachments[i]);
    ser.write_u32(sd->colorAttachmentCount);
    for (uint32_t i = 0; i < sd->colorAttachmentCount; i++)
        write_VkAttachmentReference(ser, &sd->pColorAttachments[i]);
    ser.write_bool(sd->pResolveAttachments != nullptr);
    if (sd->pResolveAttachments) {
        for (uint32_t i = 0; i < sd->colorAttachmentCount; i++)
            write_VkAttachmentReference(ser, &sd->pResolveAttachments[i]);
    }
    ser.write_bool(sd->pDepthStencilAttachment != nullptr);
    if (sd->pDepthStencilAttachment)
        write_VkAttachmentReference(ser, sd->pDepthStencilAttachment);
    ser.write_u32(sd->preserveAttachmentCount);
    for (uint32_t i = 0; i < sd->preserveAttachmentCount; i++)
        ser.write_u32(sd->pPreserveAttachments[i]);
}

// ── SubpassDependency (sub-struct for RenderPass) ──────────────────
static void write_VkSubpassDependency(VulkanSerializer& ser, const VkSubpassDependency* dep) {
    ser.write_u32(dep->srcSubpass);
    ser.write_u32(dep->dstSubpass);
    ser.write_u64(static_cast<uint64_t>(dep->srcStageMask));
    ser.write_u64(static_cast<uint64_t>(dep->dstStageMask));
    ser.write_u64(static_cast<uint64_t>(dep->srcAccessMask));
    ser.write_u64(static_cast<uint64_t>(dep->dstAccessMask));
    ser.write_u32(static_cast<uint32_t>(dep->dependencyFlags));
}

// ── RenderPass ─────────────────────────────────────────────────────
void write_VkRenderPassCreateInfo(VulkanSerializer& ser, const VkRenderPassCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->attachmentCount);
    for (uint32_t i = 0; i < info->attachmentCount; i++)
        write_VkAttachmentDescription(ser, &info->pAttachments[i]);
    ser.write_u32(info->subpassCount);
    for (uint32_t i = 0; i < info->subpassCount; i++)
        write_VkSubpassDescription(ser, &info->pSubpasses[i]);
    ser.write_u32(info->dependencyCount);
    for (uint32_t i = 0; i < info->dependencyCount; i++)
        write_VkSubpassDependency(ser, &info->pDependencies[i]);
}

// ── Framebuffer ────────────────────────────────────────────────────
void write_VkFramebufferCreateInfo(VulkanSerializer& ser, const VkFramebufferCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_handle(handle_to_u64(info->renderPass));
    ser.write_u32(info->attachmentCount);
    for (uint32_t i = 0; i < info->attachmentCount; i++)
        ser.write_handle(handle_to_u64(info->pAttachments[i]));
    ser.write_u32(info->width);
    ser.write_u32(info->height);
    ser.write_u32(info->layers);
}

// ── PipelineLayout ─────────────────────────────────────────────────
void write_VkPipelineLayoutCreateInfo(VulkanSerializer& ser, const VkPipelineLayoutCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->setLayoutCount);
    for (uint32_t i = 0; i < info->setLayoutCount; i++)
        ser.write_handle(handle_to_u64(info->pSetLayouts[i]));
    ser.write_u32(info->pushConstantRangeCount);
    for (uint32_t i = 0; i < info->pushConstantRangeCount; i++)
        ser.write_raw(&info->pPushConstantRanges[i], sizeof(VkPushConstantRange));
}

// ── DescriptorSetLayout ────────────────────────────────────────────
void write_VkDescriptorSetLayoutCreateInfo(VulkanSerializer& ser, const VkDescriptorSetLayoutCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->bindingCount);
    for (uint32_t i = 0; i < info->bindingCount; i++) {
        auto& b = info->pBindings[i];
        ser.write_u32(b.binding);
        ser.write_u32(static_cast<uint32_t>(b.descriptorType));
        ser.write_u32(b.descriptorCount);
        ser.write_u32(static_cast<uint32_t>(b.stageFlags));
        ser.write_bool(b.pImmutableSamplers != nullptr);
        if (b.pImmutableSamplers) {
            for (uint32_t si = 0; si < b.descriptorCount; si++)
                ser.write_handle(handle_to_u64(b.pImmutableSamplers[si]));
        }
    }
}

// ── DescriptorPool ─────────────────────────────────────────────────
void write_VkDescriptorPoolCreateInfo(VulkanSerializer& ser, const VkDescriptorPoolCreateInfo* info) {
    ser.write_u32(info->flags);
    ser.write_u32(info->maxSets);
    ser.write_u32(info->poolSizeCount);
    for (uint32_t i = 0; i < info->poolSizeCount; i++) {
        ser.write_u32(static_cast<uint32_t>(info->pPoolSizes[i].type));
        ser.write_u32(info->pPoolSizes[i].descriptorCount);
    }
}

// ── DescriptorSetAllocate ──────────────────────────────────────────
void write_VkDescriptorSetAllocateInfo(VulkanSerializer& ser, const VkDescriptorSetAllocateInfo* info) {
    ser.write_handle(handle_to_u64(info->descriptorPool));
    ser.write_u32(info->descriptorSetCount);
    for (uint32_t i = 0; i < info->descriptorSetCount; i++)
        ser.write_handle(handle_to_u64(info->pSetLayouts[i]));
}

// ── Swapchain ──────────────────────────────────────────────────────
void write_VkSwapchainCreateInfoKHR(VulkanSerializer& ser, const VkSwapchainCreateInfoKHR* info) {
    ser.write_u32(info->flags);
    ser.write_handle(handle_to_u64(info->surface));
    ser.write_u32(info->minImageCount);
    ser.write_u32(static_cast<uint32_t>(info->imageFormat));
    ser.write_u32(static_cast<uint32_t>(info->imageColorSpace));
    ser.write_raw(&info->imageExtent, sizeof(VkExtent2D));
    ser.write_u32(info->imageArrayLayers);
    ser.write_u32(static_cast<uint32_t>(info->imageUsage));
    ser.write_u32(static_cast<uint32_t>(info->imageSharingMode));
    write_array_t(ser, info->pQueueFamilyIndices, info->queueFamilyIndexCount);
    ser.write_u32(static_cast<uint32_t>(info->preTransform));
    ser.write_u32(static_cast<uint32_t>(info->compositeAlpha));
    ser.write_u32(static_cast<uint32_t>(info->presentMode));
    ser.write_bool(info->clipped);
    ser.write_handle(handle_to_u64(info->oldSwapchain));
}

// ── MemoryAllocate ─────────────────────────────────────────────────
void write_VkMemoryAllocateInfo(VulkanSerializer& ser, const VkMemoryAllocateInfo* info) {
    ser.write_u64(static_cast<uint64_t>(info->allocationSize));
    ser.write_u32(info->memoryTypeIndex);

    // Serialize known pNext extensions
    bool hasDedicated = false, hasFlags = false;
    VkMemoryDedicatedAllocateInfo dedicated{};
    VkMemoryAllocateFlagsInfo flags{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;

    const VkBaseInStructure* next = reinterpret_cast<const VkBaseInStructure*>(info->pNext);
    while (next) {
        if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO) {
            const auto* dai = reinterpret_cast<const VkMemoryDedicatedAllocateInfo*>(next);
            dedicated.image = dai->image;
            dedicated.buffer = dai->buffer;
            hasDedicated = true;
        } else if (next->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO) {
            const auto* afi = reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(next);
            flags.flags = afi->flags;
            hasFlags = true;
        }
        next = reinterpret_cast<const VkBaseInStructure*>(next->pNext);
    }

    ser.write_bool(hasDedicated ? VK_TRUE : VK_FALSE);
    if (hasDedicated) {
        ser.write_handle(handle_to_u64(dedicated.image));
        ser.write_handle(handle_to_u64(dedicated.buffer));
    }
    ser.write_bool(hasFlags ? VK_TRUE : VK_FALSE);
    if (hasFlags) {
        ser.write_u64(flags.flags);
    }
}

} // namespace omnigpu::serializer
