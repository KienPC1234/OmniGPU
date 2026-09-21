#pragma once

#include "vulkan_deserializer.h"
#include <vulkan/vulkan.h>

namespace omnigpu::host {

// Read back complex Vulkan structs serialized by VulkanStructSerializer.

bool read_VkSpecializationInfo(VulkanDeserializer& d, VkSpecializationInfo* out);
bool read_VkPipelineShaderStageCreateInfo(VulkanDeserializer& d, VkPipelineShaderStageCreateInfo* out);
bool read_VkComputePipelineCreateInfo(VulkanDeserializer& d, VkComputePipelineCreateInfo* out);
void free_VkComputePipelineCreateInfo(VkComputePipelineCreateInfo* info);
bool read_VkWriteDescriptorSet(VulkanDeserializer& d, VkWriteDescriptorSet* out,
                                VkDescriptorImageInfo** outImg, VkDescriptorBufferInfo** outBuf,
                                VkBufferView** outView);
void free_VkWriteDescriptorSet(VkWriteDescriptorSet* info,
                                VkDescriptorImageInfo* img, VkDescriptorBufferInfo* buf,
                                VkBufferView* view);

bool read_VkCommandPoolCreateInfo(VulkanDeserializer& d, VkCommandPoolCreateInfo* out);
bool read_VkBufferCreateInfo(VulkanDeserializer& d, VkBufferCreateInfo* out);
bool read_VkImageCreateInfo(VulkanDeserializer& d, VkImageCreateInfo* out);
bool read_VkImageViewCreateInfo(VulkanDeserializer& d, VkImageViewCreateInfo* out);
bool read_VkSamplerCreateInfo(VulkanDeserializer& d, VkSamplerCreateInfo* out);
bool read_VkPipelineLayoutCreateInfo(VulkanDeserializer& d, VkPipelineLayoutCreateInfo* out);
void free_VkPipelineLayoutCreateInfo(VkPipelineLayoutCreateInfo* info);
bool read_VkDescriptorSetLayoutCreateInfo(VulkanDeserializer& d, VkDescriptorSetLayoutCreateInfo* out);
void free_VkDescriptorSetLayoutCreateInfo(VkDescriptorSetLayoutCreateInfo* info);
bool read_VkDescriptorPoolCreateInfo(VulkanDeserializer& d, VkDescriptorPoolCreateInfo* out);
bool read_VkDescriptorSetAllocateInfo(VulkanDeserializer& d, VkDescriptorSetAllocateInfo* out);
bool read_VkMemoryAllocateInfo(VulkanDeserializer& d, VkMemoryAllocateInfo* out);
bool read_VkSubmitInfo(VulkanDeserializer& d, VkSubmitInfo* out);
void free_VkSubmitInfo(VkSubmitInfo* info);
bool read_VkSubmitInfo2(VulkanDeserializer& d, VkSubmitInfo2* out);
void free_VkSubmitInfo2(VkSubmitInfo2* info);

// Timeline semaphore
bool read_VkSemaphoreWaitInfo(VulkanDeserializer& d, VkSemaphoreWaitInfo* out);
void free_VkSemaphoreWaitInfo(VkSemaphoreWaitInfo* info);
bool read_VkSemaphoreSignalInfo(VulkanDeserializer& d, VkSemaphoreSignalInfo* out);

bool read_VkDependencyInfo(VulkanDeserializer& d, VkDependencyInfo* out);
void free_VkDependencyInfo(VkDependencyInfo* info);

} // namespace omnigpu::host
