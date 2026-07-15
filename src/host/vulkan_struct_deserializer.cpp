#include "vulkan_struct_deserializer.h"
#include <cstdlib>
#include <cstring>

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
    out->module = reinterpret_cast<VkShaderModule>(static_cast<uint64_t>(d.read_handle()));
    auto nameStr = d.read_string();
    thread_local static std::string s_name;
    s_name = nameStr;
    out->pName = s_name.c_str();
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
    if (out->viewportCount > 0) {
        out->pViewports = new VkViewport[out->viewportCount];
        d.read_raw(const_cast<VkViewport*>(out->pViewports), out->viewportCount * sizeof(VkViewport));
    }
    out->scissorCount = d.read_u32();
    if (out->scissorCount > 0) {
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
    uint32_t sampleMask = 0;
    d.read_raw(&sampleMask, sizeof(uint32_t));
    // Can't persist pSampleMask pointer easily - skip for now
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
    if (out->stageCount > 0) {
        out->pStages = new VkPipelineShaderStageCreateInfo[out->stageCount];
        for (uint32_t i = 0; i < out->stageCount; i++)
            read_VkPipelineShaderStageCreateInfo(d, const_cast<VkPipelineShaderStageCreateInfo*>(&out->pStages[i]));
    }
    VkPipelineVertexInputStateCreateInfo* vis = new VkPipelineVertexInputStateCreateInfo;
    read_VkPipelineVertexInputStateCreateInfo(d, vis);
    out->pVertexInputState = vis;
    VkPipelineInputAssemblyStateCreateInfo* ias = new VkPipelineInputAssemblyStateCreateInfo;
    read_VkPipelineInputAssemblyStateCreateInfo(d, ias);
    out->pInputAssemblyState = ias;
    VkPipelineTessellationStateCreateInfo* ts = new VkPipelineTessellationStateCreateInfo;
    read_VkPipelineTessellationStateCreateInfo(d, ts);
    out->pTessellationState = ts;
    VkPipelineViewportStateCreateInfo* vs = new VkPipelineViewportStateCreateInfo;
    read_VkPipelineViewportStateCreateInfo(d, vs);
    out->pViewportState = vs;
    VkPipelineRasterizationStateCreateInfo* rs = new VkPipelineRasterizationStateCreateInfo;
    read_VkPipelineRasterizationStateCreateInfo(d, rs);
    out->pRasterizationState = rs;
    VkPipelineMultisampleStateCreateInfo* ms = new VkPipelineMultisampleStateCreateInfo;
    read_VkPipelineMultisampleStateCreateInfo(d, ms);
    out->pMultisampleState = ms;
    VkPipelineDepthStencilStateCreateInfo* ds = new VkPipelineDepthStencilStateCreateInfo;
    read_VkPipelineDepthStencilStateCreateInfo(d, ds);
    out->pDepthStencilState = ds;
    VkPipelineColorBlendStateCreateInfo* cbs = new VkPipelineColorBlendStateCreateInfo;
    read_VkPipelineColorBlendStateCreateInfo(d, cbs);
    out->pColorBlendState = cbs;
    VkPipelineDynamicStateCreateInfo* dyn = new VkPipelineDynamicStateCreateInfo;
    read_VkPipelineDynamicStateCreateInfo(d, dyn);
    out->pDynamicState = dyn;
    out->layout = reinterpret_cast<VkPipelineLayout>(static_cast<uint64_t>(d.read_handle()));
    out->renderPass = reinterpret_cast<VkRenderPass>(static_cast<uint64_t>(d.read_handle()));
    out->subpass = d.read_u32();
    out->basePipelineHandle = reinterpret_cast<VkPipeline>(static_cast<uint64_t>(d.read_handle()));
    out->basePipelineIndex = d.read_i32();
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
    out->layout = reinterpret_cast<VkPipelineLayout>(static_cast<uint64_t>(d.read_handle()));
    out->basePipelineHandle = reinterpret_cast<VkPipeline>(static_cast<uint64_t>(d.read_handle()));
    out->basePipelineIndex = d.read_i32();
    return d.ok();
}

bool read_VkWriteDescriptorSet(VulkanDeserializer& d, VkWriteDescriptorSet* out,
                                VkDescriptorImageInfo** outImg, VkDescriptorBufferInfo** outBuf,
                                VkBufferView** outView) {
    std::memset(out, 0, sizeof(*out));
    out->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    if (!is_present(d)) return false;
    out->dstSet = reinterpret_cast<VkDescriptorSet>(static_cast<uint64_t>(d.read_handle()));
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
            (*outView)[i] = reinterpret_cast<VkBufferView>(static_cast<uint64_t>(d.read_handle()));
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
    out->imageView = reinterpret_cast<VkImageView>(static_cast<uint64_t>(d.read_handle()));
    out->imageLayout = static_cast<VkImageLayout>(d.read_u32());
    out->resolveMode = static_cast<VkResolveModeFlagBits>(d.read_u32());
    out->resolveImageView = reinterpret_cast<VkImageView>(static_cast<uint64_t>(d.read_handle()));
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

} // namespace omnigpu::host
