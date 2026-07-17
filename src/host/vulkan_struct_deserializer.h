#pragma once

#include "vulkan_deserializer.h"
#include <vulkan/vulkan.h>

namespace omnigpu::host {

// Read back complex Vulkan structs serialized by VulkanStructSerializer.

bool read_VkSpecializationInfo(VulkanDeserializer& d, VkSpecializationInfo* out);
bool read_VkPipelineShaderStageCreateInfo(VulkanDeserializer& d, VkPipelineShaderStageCreateInfo* out);
bool read_VkPipelineVertexInputStateCreateInfo(VulkanDeserializer& d, VkPipelineVertexInputStateCreateInfo* out);
bool read_VkPipelineInputAssemblyStateCreateInfo(VulkanDeserializer& d, VkPipelineInputAssemblyStateCreateInfo* out);
bool read_VkPipelineTessellationStateCreateInfo(VulkanDeserializer& d, VkPipelineTessellationStateCreateInfo* out);
bool read_VkPipelineViewportStateCreateInfo(VulkanDeserializer& d, VkPipelineViewportStateCreateInfo* out);
bool read_VkPipelineRasterizationStateCreateInfo(VulkanDeserializer& d, VkPipelineRasterizationStateCreateInfo* out);
bool read_VkPipelineMultisampleStateCreateInfo(VulkanDeserializer& d, VkPipelineMultisampleStateCreateInfo* out);
bool read_VkPipelineDepthStencilStateCreateInfo(VulkanDeserializer& d, VkPipelineDepthStencilStateCreateInfo* out);
bool read_VkPipelineColorBlendAttachmentState(VulkanDeserializer& d, VkPipelineColorBlendAttachmentState* out);
bool read_VkPipelineColorBlendStateCreateInfo(VulkanDeserializer& d, VkPipelineColorBlendStateCreateInfo* out);
bool read_VkPipelineDynamicStateCreateInfo(VulkanDeserializer& d, VkPipelineDynamicStateCreateInfo* out);

bool read_VkGraphicsPipelineCreateInfo(VulkanDeserializer& d, VkGraphicsPipelineCreateInfo* out);
void free_VkGraphicsPipelineCreateInfo(VkGraphicsPipelineCreateInfo* info);

bool read_VkComputePipelineCreateInfo(VulkanDeserializer& d, VkComputePipelineCreateInfo* out);

bool read_VkWriteDescriptorSet(VulkanDeserializer& d, VkWriteDescriptorSet* out,
                                VkDescriptorImageInfo** outImg, VkDescriptorBufferInfo** outBuf,
                                VkBufferView** outView);
void free_VkWriteDescriptorSet(VkWriteDescriptorSet* info,
                                VkDescriptorImageInfo* img, VkDescriptorBufferInfo* buf,
                                VkBufferView* view);

bool read_VkRenderingAttachmentInfo(VulkanDeserializer& d, VkRenderingAttachmentInfo* out);
bool read_VkRenderingInfo(VulkanDeserializer& d, VkRenderingInfo* out);
void free_VkRenderingInfo(VkRenderingInfo* info);

bool read_VkRenderPassBeginInfo(VulkanDeserializer& d, VkRenderPassBeginInfo* out);

bool read_VkCommandPoolCreateInfo(VulkanDeserializer& d, VkCommandPoolCreateInfo* out);
bool read_VkBufferCreateInfo(VulkanDeserializer& d, VkBufferCreateInfo* out);
bool read_VkImageCreateInfo(VulkanDeserializer& d, VkImageCreateInfo* out);
bool read_VkImageViewCreateInfo(VulkanDeserializer& d, VkImageViewCreateInfo* out);
bool read_VkSamplerCreateInfo(VulkanDeserializer& d, VkSamplerCreateInfo* out);
bool read_VkRenderPassCreateInfo(VulkanDeserializer& d, VkRenderPassCreateInfo* out);
void free_VkRenderPassCreateInfo(VkRenderPassCreateInfo* info);
bool read_VkFramebufferCreateInfo(VulkanDeserializer& d, VkFramebufferCreateInfo* out);
void free_VkFramebufferCreateInfo(VkFramebufferCreateInfo* info);
bool read_VkPipelineLayoutCreateInfo(VulkanDeserializer& d, VkPipelineLayoutCreateInfo* out);
void free_VkPipelineLayoutCreateInfo(VkPipelineLayoutCreateInfo* info);
bool read_VkDescriptorSetLayoutCreateInfo(VulkanDeserializer& d, VkDescriptorSetLayoutCreateInfo* out);
void free_VkDescriptorSetLayoutCreateInfo(VkDescriptorSetLayoutCreateInfo* info);
bool read_VkDescriptorPoolCreateInfo(VulkanDeserializer& d, VkDescriptorPoolCreateInfo* out);
bool read_VkDescriptorSetAllocateInfo(VulkanDeserializer& d, VkDescriptorSetAllocateInfo* out);
bool read_VkSwapchainCreateInfoKHR(VulkanDeserializer& d, VkSwapchainCreateInfoKHR* out);
bool read_VkMemoryAllocateInfo(VulkanDeserializer& d, VkMemoryAllocateInfo* out);
bool read_VkSubmitInfo(VulkanDeserializer& d, VkSubmitInfo* out);
void free_VkSubmitInfo(VkSubmitInfo* info);
bool read_VkSubmitInfo2(VulkanDeserializer& d, VkSubmitInfo2* out);
void free_VkSubmitInfo2(VkSubmitInfo2* info);

} // namespace omnigpu::host
