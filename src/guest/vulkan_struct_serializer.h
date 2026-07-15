#pragma once

#include "vulkan_serializer.h"
#include <vulkan/vulkan.h>

namespace omnigpu::serializer {

// Serializers for structs with embedded pointer-to-data fields.
// These follow each data pointer so the host can reconstruct the full struct.

void write_VkShaderModuleCreateInfo(
    VulkanSerializer& ser, const VkShaderModuleCreateInfo* info);

void write_VkSemaphoreCreateInfo(
    VulkanSerializer& ser, const VkSemaphoreCreateInfo* info);

void write_VkDescriptorUpdateTemplateCreateInfo(
    VulkanSerializer& ser, const VkDescriptorUpdateTemplateCreateInfo* info);

void write_VkPipelineShaderStageCreateInfo(
    VulkanSerializer& ser, const VkPipelineShaderStageCreateInfo* info);
void write_VkPipelineVertexInputStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineVertexInputStateCreateInfo* info);
void write_VkPipelineInputAssemblyStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineInputAssemblyStateCreateInfo* info);
void write_VkPipelineTessellationStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineTessellationStateCreateInfo* info);
void write_VkPipelineViewportStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineViewportStateCreateInfo* info);
void write_VkPipelineRasterizationStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineRasterizationStateCreateInfo* info);
void write_VkPipelineMultisampleStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineMultisampleStateCreateInfo* info);
void write_VkPipelineDepthStencilStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineDepthStencilStateCreateInfo* info);
void write_VkPipelineColorBlendStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineColorBlendStateCreateInfo* info);
void write_VkPipelineColorBlendAttachmentState(
    VulkanSerializer& ser, const VkPipelineColorBlendAttachmentState* state);
void write_VkPipelineDynamicStateCreateInfo(
    VulkanSerializer& ser, const VkPipelineDynamicStateCreateInfo* info);
void write_VkSpecializationInfo(
    VulkanSerializer& ser, const VkSpecializationInfo* info);

void write_VkGraphicsPipelineCreateInfo(
    VulkanSerializer& ser, const VkGraphicsPipelineCreateInfo* info);
void write_VkComputePipelineCreateInfo(
    VulkanSerializer& ser, const VkComputePipelineCreateInfo* info);

void write_VkWriteDescriptorSet(
    VulkanSerializer& ser, const VkWriteDescriptorSet* info);

void write_VkRenderingInfo(
    VulkanSerializer& ser, const VkRenderingInfo* info);

void write_VkSubmitInfo(
    VulkanSerializer& ser, const VkSubmitInfo* info);
void write_VkSubmitInfo2(
    VulkanSerializer& ser, const VkSubmitInfo2* info);

void write_VkDependencyInfo(
    VulkanSerializer& ser, const VkDependencyInfo* info);

void write_VkRenderPassBeginInfo(
    VulkanSerializer& ser, const VkRenderPassBeginInfo* info);

} // namespace omnigpu::serializer
