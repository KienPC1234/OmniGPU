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

bool read_VkPipelineVertexInputStateCreateInfo(VulkanDeserializer& d, VkPipelineVertexInputStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->vertexBindingDescriptionCount = d.read_u32();
    if (out->vertexBindingDescriptionCount > 0) {
        out->pVertexBindingDescriptions = new VkVertexInputBindingDescription[out->vertexBindingDescriptionCount];
        d.read_raw(const_cast<VkVertexInputBindingDescription*>(out->pVertexBindingDescriptions),
                   out->vertexBindingDescriptionCount * sizeof(VkVertexInputBindingDescription));
    }
    out->vertexAttributeDescriptionCount = d.read_u32();
    if (out->vertexAttributeDescriptionCount > 0) {
        out->pVertexAttributeDescriptions = new VkVertexInputAttributeDescription[out->vertexAttributeDescriptionCount];
        d.read_raw(const_cast<VkVertexInputAttributeDescription*>(out->pVertexAttributeDescriptions),
                   out->vertexAttributeDescriptionCount * sizeof(VkVertexInputAttributeDescription));
    }
    return d.ok();
}

bool read_VkPipelineInputAssemblyStateCreateInfo(VulkanDeserializer& d, VkPipelineInputAssemblyStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->topology = static_cast<VkPrimitiveTopology>(d.read_u32());
    out->primitiveRestartEnable = d.read_bool();
    return d.ok();
}

bool read_VkPipelineTessellationStateCreateInfo(VulkanDeserializer& d, VkPipelineTessellationStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->patchControlPoints = d.read_u32();
    return d.ok();
}

bool read_VkPipelineViewportStateCreateInfo(VulkanDeserializer& d, VkPipelineViewportStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->viewportCount = d.read_u32();
    bool hasViewports = d.read_bool();
    if (hasViewports && out->viewportCount > 0) {
        out->pViewports = new VkViewport[out->viewportCount];
        d.read_raw(const_cast<VkViewport*>(out->pViewports), out->viewportCount * sizeof(VkViewport));
    }
    out->scissorCount = d.read_u32();
    bool hasScissors = d.read_bool();
    if (hasScissors && out->scissorCount > 0) {
        out->pScissors = new VkRect2D[out->scissorCount];
        d.read_raw(const_cast<VkRect2D*>(out->pScissors), out->scissorCount * sizeof(VkRect2D));
    }
    return d.ok();
}

bool read_VkPipelineRasterizationStateCreateInfo(VulkanDeserializer& d, VkPipelineRasterizationStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->depthClampEnable = d.read_bool();
    out->rasterizerDiscardEnable = d.read_bool();
    out->polygonMode = static_cast<VkPolygonMode>(d.read_u32());
    out->cullMode = d.read_u32();
    out->frontFace = static_cast<VkFrontFace>(d.read_u32());
    out->depthBiasEnable = d.read_bool();
    out->depthBiasConstantFactor = d.read_f32();
    out->depthBiasClamp = d.read_f32();
    out->depthBiasSlopeFactor = d.read_f32();
    out->lineWidth = d.read_f32();
    return d.ok();
}

bool read_VkPipelineMultisampleStateCreateInfo(VulkanDeserializer& d, VkPipelineMultisampleStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->rasterizationSamples = static_cast<VkSampleCountFlagBits>(d.read_u32());
    out->sampleShadingEnable = d.read_bool();
    out->minSampleShading = d.read_f32();
    // Read presence flag then optional pSampleMask data
    bool hasSampleMask = d.read_bool();
    if (hasSampleMask) {
        uint32_t* mask = new uint32_t;
        d.read_raw(mask, sizeof(uint32_t));
        out->pSampleMask = mask;
    }
    out->alphaToCoverageEnable = d.read_bool();
    out->alphaToOneEnable = d.read_bool();
    return d.ok();
}

bool read_VkPipelineDepthStencilStateCreateInfo(VulkanDeserializer& d, VkPipelineDepthStencilStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->depthTestEnable = d.read_bool();
    out->depthWriteEnable = d.read_bool();
    out->depthCompareOp = static_cast<VkCompareOp>(d.read_u32());
    out->depthBoundsTestEnable = d.read_bool();
    out->stencilTestEnable = d.read_bool();
    d.read_raw(&out->front, sizeof(VkStencilOpState));
    d.read_raw(&out->back, sizeof(VkStencilOpState));
    out->minDepthBounds = d.read_f32();
    out->maxDepthBounds = d.read_f32();
    return d.ok();
}

bool read_VkPipelineColorBlendAttachmentState(VulkanDeserializer& d, VkPipelineColorBlendAttachmentState* out) {
    out->blendEnable = d.read_bool();
    out->srcColorBlendFactor = static_cast<VkBlendFactor>(d.read_u32());
    out->dstColorBlendFactor = static_cast<VkBlendFactor>(d.read_u32());
    out->colorBlendOp = static_cast<VkBlendOp>(d.read_u32());
    out->srcAlphaBlendFactor = static_cast<VkBlendFactor>(d.read_u32());
    out->dstAlphaBlendFactor = static_cast<VkBlendFactor>(d.read_u32());
    out->alphaBlendOp = static_cast<VkBlendOp>(d.read_u32());
    out->colorWriteMask = d.read_u32();
    return d.ok();
}

bool read_VkPipelineColorBlendStateCreateInfo(VulkanDeserializer& d, VkPipelineColorBlendStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->logicOpEnable = d.read_bool();
    out->logicOp = static_cast<VkLogicOp>(d.read_u32());
    out->attachmentCount = d.read_u32();
    if (out->attachmentCount > 0) {
        out->pAttachments = new VkPipelineColorBlendAttachmentState[out->attachmentCount];
        for (uint32_t i = 0; i < out->attachmentCount; i++)
            read_VkPipelineColorBlendAttachmentState(d, const_cast<VkPipelineColorBlendAttachmentState*>(&out->pAttachments[i]));
    }
    d.read_raw(const_cast<float*>(out->blendConstants), sizeof(float) * 4);
    return d.ok();
}

bool read_VkPipelineDynamicStateCreateInfo(VulkanDeserializer& d, VkPipelineDynamicStateCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    if (!is_present(d)) return true;
    out->flags = d.read_u32();
    out->dynamicStateCount = d.read_u32();
    if (out->dynamicStateCount > 0) {
        out->pDynamicStates = new VkDynamicState[out->dynamicStateCount];
        for (uint32_t i = 0; i < out->dynamicStateCount; i++)
            const_cast<VkDynamicState*>(out->pDynamicStates)[i] = static_cast<VkDynamicState>(d.read_u32());
    }
    return d.ok();
}

bool read_VkGraphicsPipelineCreateInfo(VulkanDeserializer& d, VkGraphicsPipelineCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    out->flags = d.read_u32();
    out->stageCount = d.read_u32();
    SPDLOG_INFO("  [deser] flags={}, stageCount={}, d.ok={}", out->flags, out->stageCount, d.ok());
    if (out->stageCount > 0) {
        out->pStages = new VkPipelineShaderStageCreateInfo[out->stageCount]();
        for (uint32_t i = 0; i < out->stageCount; i++)
            read_VkPipelineShaderStageCreateInfo(d, const_cast<VkPipelineShaderStageCreateInfo*>(&out->pStages[i]));
    }
    SPDLOG_INFO("  [deser] after stages, d.ok={}", d.ok());
    VkPipelineVertexInputStateCreateInfo* vis = new VkPipelineVertexInputStateCreateInfo;
    read_VkPipelineVertexInputStateCreateInfo(d, vis);
    out->pVertexInputState = vis;
    SPDLOG_INFO("  [deser] after vertexInput, d.ok={}", d.ok());
    VkPipelineInputAssemblyStateCreateInfo* ias = new VkPipelineInputAssemblyStateCreateInfo;
    read_VkPipelineInputAssemblyStateCreateInfo(d, ias);
    out->pInputAssemblyState = ias;
    SPDLOG_INFO("  [deser] after inputAssembly, d.ok={}", d.ok());
    VkPipelineTessellationStateCreateInfo* ts = new VkPipelineTessellationStateCreateInfo;
    read_VkPipelineTessellationStateCreateInfo(d, ts);
    out->pTessellationState = ts;
    SPDLOG_INFO("  [deser] after tessellation, d.ok={}", d.ok());
    VkPipelineViewportStateCreateInfo* vs = new VkPipelineViewportStateCreateInfo;
    read_VkPipelineViewportStateCreateInfo(d, vs);
    out->pViewportState = vs;
    SPDLOG_INFO("  [deser] after viewport, d.ok={}", d.ok());
    VkPipelineRasterizationStateCreateInfo* rs = new VkPipelineRasterizationStateCreateInfo;
    read_VkPipelineRasterizationStateCreateInfo(d, rs);
    out->pRasterizationState = rs;
    SPDLOG_INFO("  [deser] after rasterization, d.ok={}", d.ok());
    VkPipelineMultisampleStateCreateInfo* ms = new VkPipelineMultisampleStateCreateInfo;
    read_VkPipelineMultisampleStateCreateInfo(d, ms);
    out->pMultisampleState = ms;
    SPDLOG_INFO("  [deser] after multisample, d.ok={}", d.ok());
    VkPipelineDepthStencilStateCreateInfo* ds = new VkPipelineDepthStencilStateCreateInfo;
    read_VkPipelineDepthStencilStateCreateInfo(d, ds);
    out->pDepthStencilState = ds;
    SPDLOG_INFO("  [deser] after depthStencil, d.ok={}", d.ok());
    VkPipelineColorBlendStateCreateInfo* cbs = new VkPipelineColorBlendStateCreateInfo;
    read_VkPipelineColorBlendStateCreateInfo(d, cbs);
    out->pColorBlendState = cbs;
    SPDLOG_INFO("  [deser] after colorBlend, d.ok={}", d.ok());
    VkPipelineDynamicStateCreateInfo* dyn = new VkPipelineDynamicStateCreateInfo;
    read_VkPipelineDynamicStateCreateInfo(d, dyn);
    out->pDynamicState = dyn;
    SPDLOG_INFO("  [deser] after dynamicState, d.ok={}", d.ok());
    uint64_t rawLayout = d.read_handle();
    out->layout = handle_from_u64<VkPipelineLayout>(rawLayout);
    uint64_t rawRenderPass = d.read_handle();
    out->renderPass = handle_from_u64<VkRenderPass>(rawRenderPass);
    out->subpass = d.read_u32();
    uint64_t rawBase = d.read_handle();
    out->basePipelineHandle = handle_from_u64<VkPipeline>(rawBase);
    out->basePipelineIndex = d.read_i32();
    SPDLOG_INFO("  [deser] layout_raw={}, renderPass_raw={}, base_raw={}, subpass={}, d.ok={}",
        rawLayout, rawRenderPass, rawBase, out->subpass, d.ok());
    return d.ok();
}

void free_VkGraphicsPipelineCreateInfo(VkGraphicsPipelineCreateInfo* info) {
    if (!info) return;

    // Free shader stages and their specialization info
    if (info->pStages) {
        for (uint32_t i = 0; i < info->stageCount; i++) {
            auto* spec = const_cast<VkSpecializationInfo*>(info->pStages[i].pSpecializationInfo);
            if (spec) {
                delete[] spec->pMapEntries;
                std::free(const_cast<void*>(spec->pData));
                delete spec;
            }
        }
        delete[] info->pStages;
    }

    // Free vertex input state sub-arrays
    if (info->pVertexInputState) {
        delete[] info->pVertexInputState->pVertexBindingDescriptions;
        delete[] info->pVertexInputState->pVertexAttributeDescriptions;
        delete info->pVertexInputState;
    }

    delete info->pInputAssemblyState;
    delete info->pTessellationState;

    // Free viewport state sub-arrays
    if (info->pViewportState) {
        delete[] info->pViewportState->pViewports;
        delete[] info->pViewportState->pScissors;
        delete info->pViewportState;
    }

    delete info->pRasterizationState;

    // Free multisample state (pSampleMask is optional)
    delete info->pMultisampleState;
    delete info->pDepthStencilState;

    // Free color blend state sub-arrays
    if (info->pColorBlendState) {
        delete[] info->pColorBlendState->pAttachments;
        delete info->pColorBlendState;
    }

    // Free dynamic state sub-array
    if (info->pDynamicState) {
        delete[] info->pDynamicState->pDynamicStates;
        delete info->pDynamicState;
    }

    std::memset(info, 0, sizeof(*info));
}

void free_VkRenderingInfo(VkRenderingInfo* info) {
    if (!info) return;
    delete[] info->pColorAttachments;
    delete info->pDepthAttachment;
    delete info->pStencilAttachment;
    std::memset(info, 0, sizeof(*info));
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
        for (uint32_t i = 0; i < count; i++)
            d.read_raw(&(*outImg)[i], sizeof(VkDescriptorImageInfo));
        *outBuf = new VkDescriptorBufferInfo[count];
        for (uint32_t i = 0; i < count; i++)
            d.read_raw(&(*outBuf)[i], sizeof(VkDescriptorBufferInfo));
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

bool read_VkRenderingAttachmentInfo(VulkanDeserializer& d, VkRenderingAttachmentInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    out->imageView = handle_from_u64<VkImageView>(d.read_handle());
    out->imageLayout = static_cast<VkImageLayout>(d.read_u32());
    out->resolveMode = static_cast<VkResolveModeFlagBits>(d.read_u32());
    out->resolveImageView = handle_from_u64<VkImageView>(d.read_handle());
    out->resolveImageLayout = static_cast<VkImageLayout>(d.read_u32());
    out->loadOp = static_cast<VkAttachmentLoadOp>(d.read_u32());
    out->storeOp = static_cast<VkAttachmentStoreOp>(d.read_u32());
    d.read_raw(&out->clearValue, sizeof(VkClearValue));
    return d.ok();
}

bool read_VkRenderingInfo(VulkanDeserializer& d, VkRenderingInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    out->flags = d.read_u32();
    d.read_raw(&out->renderArea, sizeof(VkRect2D));
    out->layerCount = d.read_u32();
    out->viewMask = d.read_u32();
    out->colorAttachmentCount = d.read_u32();
    if (out->colorAttachmentCount > 0) {
        out->pColorAttachments = new VkRenderingAttachmentInfo[out->colorAttachmentCount];
        for (uint32_t i = 0; i < out->colorAttachmentCount; i++)
            read_VkRenderingAttachmentInfo(d, const_cast<VkRenderingAttachmentInfo*>(&out->pColorAttachments[i]));
    }
    if (is_present(d)) {
        VkRenderingAttachmentInfo* da = new VkRenderingAttachmentInfo;
        read_VkRenderingAttachmentInfo(d, da);
        out->pDepthAttachment = da;
    }
    if (is_present(d)) {
        VkRenderingAttachmentInfo* sa = new VkRenderingAttachmentInfo;
        read_VkRenderingAttachmentInfo(d, sa);
        out->pStencilAttachment = sa;
    }
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

bool read_VkRenderPassCreateInfo(VulkanDeserializer& d, VkRenderPassCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    out->flags = static_cast<VkRenderPassCreateFlags>(d.read_u32());
    out->attachmentCount = d.read_u32();
    if (out->attachmentCount > 0) {
        out->pAttachments = new VkAttachmentDescription[out->attachmentCount];
        for (uint32_t i = 0; i < out->attachmentCount; i++) {
            auto* a = const_cast<VkAttachmentDescription*>(&out->pAttachments[i]);
            a->flags = d.read_u32();
            a->format = static_cast<VkFormat>(d.read_u32());
            a->samples = static_cast<VkSampleCountFlagBits>(d.read_u32());
            a->loadOp = static_cast<VkAttachmentLoadOp>(d.read_u32());
            a->storeOp = static_cast<VkAttachmentStoreOp>(d.read_u32());
            a->stencilLoadOp = static_cast<VkAttachmentLoadOp>(d.read_u32());
            a->stencilStoreOp = static_cast<VkAttachmentStoreOp>(d.read_u32());
            a->initialLayout = static_cast<VkImageLayout>(d.read_u32());
            a->finalLayout = static_cast<VkImageLayout>(d.read_u32());
        }
    }
    out->subpassCount = d.read_u32();
    if (out->subpassCount > 0) {
        out->pSubpasses = new VkSubpassDescription[out->subpassCount]();
        for (uint32_t s = 0; s < out->subpassCount; s++) {
            auto* sd = const_cast<VkSubpassDescription*>(&out->pSubpasses[s]);
            sd->flags = static_cast<VkSubpassDescriptionFlags>(d.read_u32());
            sd->pipelineBindPoint = static_cast<VkPipelineBindPoint>(d.read_u32());
            sd->inputAttachmentCount = d.read_u32();
            if (sd->inputAttachmentCount > 0) {
                sd->pInputAttachments = new VkAttachmentReference[sd->inputAttachmentCount];
                for (uint32_t j = 0; j < sd->inputAttachmentCount; j++) {
                    auto* r = const_cast<VkAttachmentReference*>(&sd->pInputAttachments[j]);
                    r->attachment = d.read_u32();
                    r->layout = static_cast<VkImageLayout>(d.read_u32());
                }
            }
            sd->colorAttachmentCount = d.read_u32();
            if (sd->colorAttachmentCount > 0) {
                sd->pColorAttachments = new VkAttachmentReference[sd->colorAttachmentCount];
                for (uint32_t j = 0; j < sd->colorAttachmentCount; j++) {
                    auto* r = const_cast<VkAttachmentReference*>(&sd->pColorAttachments[j]);
                    r->attachment = d.read_u32();
                    r->layout = static_cast<VkImageLayout>(d.read_u32());
                }
            }
            if (d.read_bool()) {
                VkAttachmentReference* ra = new VkAttachmentReference[sd->colorAttachmentCount];
                for (uint32_t j = 0; j < sd->colorAttachmentCount; j++) {
                    ra[j].attachment = d.read_u32();
                    ra[j].layout = static_cast<VkImageLayout>(d.read_u32());
                }
                sd->pResolveAttachments = ra;
            }
            if (d.read_bool()) {
                VkAttachmentReference* da = new VkAttachmentReference;
                da->attachment = d.read_u32();
                da->layout = static_cast<VkImageLayout>(d.read_u32());
                sd->pDepthStencilAttachment = da;
            }
            sd->preserveAttachmentCount = d.read_u32();
            if (sd->preserveAttachmentCount > 0) {
                sd->pPreserveAttachments = new uint32_t[sd->preserveAttachmentCount];
                for (uint32_t j = 0; j < sd->preserveAttachmentCount; j++)
                    const_cast<uint32_t*>(sd->pPreserveAttachments)[j] = d.read_u32();
            }
        }
    }
    out->dependencyCount = d.read_u32();
    if (out->dependencyCount > 0) {
        out->pDependencies = new VkSubpassDependency[out->dependencyCount];
        for (uint32_t i = 0; i < out->dependencyCount; i++) {
            auto* dep = const_cast<VkSubpassDependency*>(&out->pDependencies[i]);
            dep->srcSubpass = d.read_u32();
            dep->dstSubpass = d.read_u32();
            dep->srcStageMask = static_cast<VkPipelineStageFlags>(d.read_u64());
            dep->dstStageMask = static_cast<VkPipelineStageFlags>(d.read_u64());
            dep->srcAccessMask = static_cast<VkAccessFlags>(d.read_u64());
            dep->dstAccessMask = static_cast<VkAccessFlags>(d.read_u64());
            dep->dependencyFlags = static_cast<VkDependencyFlags>(d.read_u32());
        }
    }
    return d.ok();
}

void free_VkRenderPassCreateInfo(VkRenderPassCreateInfo* info) {
    if (!info) return;
    delete[] info->pAttachments;
    for (uint32_t s = 0; s < info->subpassCount; s++) {
        delete[] info->pSubpasses[s].pInputAttachments;
        delete[] info->pSubpasses[s].pColorAttachments;
        delete[] info->pSubpasses[s].pResolveAttachments;
        delete info->pSubpasses[s].pDepthStencilAttachment;
        delete[] info->pSubpasses[s].pPreserveAttachments;
    }
    delete[] info->pSubpasses;
    delete[] info->pDependencies;
    std::memset(info, 0, sizeof(*info));
}

bool read_VkFramebufferCreateInfo(VulkanDeserializer& d, VkFramebufferCreateInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    out->flags = static_cast<VkFramebufferCreateFlags>(d.read_u32());
    out->renderPass = handle_from_u64<VkRenderPass>(d.read_handle());
    out->attachmentCount = d.read_u32();
    if (out->attachmentCount > 0) {
        out->pAttachments = new VkImageView[out->attachmentCount];
        for (uint32_t i = 0; i < out->attachmentCount; i++)
            const_cast<VkImageView*>(out->pAttachments)[i] = handle_from_u64<VkImageView>(d.read_handle());
    }
    out->width = d.read_u32();
    out->height = d.read_u32();
    out->layers = d.read_u32();
    return d.ok();
}

void free_VkFramebufferCreateInfo(VkFramebufferCreateInfo* info) {
    if (!info) return;
    delete[] info->pAttachments;
    std::memset(info, 0, sizeof(*info));
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

bool read_VkSwapchainCreateInfoKHR(VulkanDeserializer& d, VkSwapchainCreateInfoKHR* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    out->flags = static_cast<VkSwapchainCreateFlagsKHR>(d.read_u32());
    out->surface = handle_from_u64<VkSurfaceKHR>(d.read_handle());
    out->minImageCount = d.read_u32();
    out->imageFormat = static_cast<VkFormat>(d.read_u32());
    out->imageColorSpace = static_cast<VkColorSpaceKHR>(d.read_u32());
    d.read_raw(&out->imageExtent, sizeof(VkExtent2D));
    out->imageArrayLayers = d.read_u32();
    out->imageUsage = static_cast<VkImageUsageFlags>(d.read_u32());
    out->imageSharingMode = static_cast<VkSharingMode>(d.read_u32());
    uint32_t qfi = d.read_u32();
    if (qfi > 0) {
        out->pQueueFamilyIndices = new uint32_t[qfi];
        d.read_raw(const_cast<uint32_t*>(out->pQueueFamilyIndices), qfi * sizeof(uint32_t));
        out->queueFamilyIndexCount = qfi;
    }
    out->preTransform = static_cast<VkSurfaceTransformFlagBitsKHR>(d.read_u32());
    out->compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(d.read_u32());
    out->presentMode = static_cast<VkPresentModeKHR>(d.read_u32());
    out->clipped = d.read_bool();
    out->oldSwapchain = handle_from_u64<VkSwapchainKHR>(d.read_handle());
    return d.ok();
}

bool read_VkRenderPassBeginInfo(VulkanDeserializer& d, VkRenderPassBeginInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    out->renderPass = handle_from_u64<VkRenderPass>(d.read_handle());
    out->framebuffer = handle_from_u64<VkFramebuffer>(d.read_handle());
    d.read_raw(&out->renderArea, sizeof(VkRect2D));
    out->clearValueCount = d.read_u32();
    if (out->clearValueCount > 0) {
        out->pClearValues = new VkClearValue[out->clearValueCount];
        for (uint32_t i = 0; i < out->clearValueCount; i++)
            d.read_raw(const_cast<VkClearValue*>(&out->pClearValues[i]), sizeof(VkClearValue));
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

} // namespace omnigpu::host
