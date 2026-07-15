#pragma once

#include <vulkan/vulkan.h>

namespace omnigpu::batch {
class CommandBatch;
}

namespace omnigpu::intercept {

void initialize_hooks();
void shutdown_hooks();
void set_batch(batch::CommandBatch* batch);
PFN_vkVoidFunction get_intercept_proc(const char* name);

// Auto-generated hooks (declared for icd_entrypoints.cpp)
#define DECL_HOOK(ret, name, ...) \
    ret VKAPI_PTR name##_hook(__VA_ARGS__)

// ---- Vulkan 1.0 Core — Auto-generated hooks ----
DECL_HOOK(VkResult, vkQueueSubmit, VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
DECL_HOOK(VkResult, vkQueuePresentKHR, VkQueue, const VkPresentInfoKHR*);
DECL_HOOK(VkResult, vkDeviceWaitIdle, VkDevice);
DECL_HOOK(VkResult, vkQueueWaitIdle, VkQueue);

DECL_HOOK(VkResult, vkCreateCommandPool, VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*);
DECL_HOOK(void, vkDestroyCommandPool, VkDevice, VkCommandPool, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkResetCommandPool, VkDevice, VkCommandPool, VkCommandPoolResetFlags);
DECL_HOOK(VkResult, vkAllocateCommandBuffers, VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
DECL_HOOK(void, vkFreeCommandBuffers, VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
DECL_HOOK(VkResult, vkBeginCommandBuffer, VkCommandBuffer, const VkCommandBufferBeginInfo*);
DECL_HOOK(VkResult, vkEndCommandBuffer, VkCommandBuffer);
DECL_HOOK(VkResult, vkResetCommandBuffer, VkCommandBuffer, VkCommandBufferResetFlags);

DECL_HOOK(VkResult, vkAllocateMemory, VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*);
DECL_HOOK(void, vkFreeMemory, VkDevice, VkDeviceMemory, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkMapMemory, VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**);
DECL_HOOK(void, vkUnmapMemory, VkDevice, VkDeviceMemory);
DECL_HOOK(VkResult, vkFlushMappedMemoryRanges, VkDevice, uint32_t, const VkMappedMemoryRange*);
DECL_HOOK(VkResult, vkInvalidateMappedMemoryRanges, VkDevice, uint32_t, const VkMappedMemoryRange*);
DECL_HOOK(VkResult, vkBindBufferMemory, VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
DECL_HOOK(VkResult, vkBindImageMemory, VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);

DECL_HOOK(VkResult, vkCreateBuffer, VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*);
DECL_HOOK(void, vkDestroyBuffer, VkDevice, VkBuffer, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkCreateImage, VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
DECL_HOOK(void, vkDestroyImage, VkDevice, VkImage, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkCreateImageView, VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);
DECL_HOOK(void, vkDestroyImageView, VkDevice, VkImageView, const VkAllocationCallbacks*);

DECL_HOOK(VkResult, vkCreateSampler, VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*);
DECL_HOOK(void, vkDestroySampler, VkDevice, VkSampler, const VkAllocationCallbacks*);

DECL_HOOK(VkResult, vkCreateShaderModule, VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*);
DECL_HOOK(void, vkDestroyShaderModule, VkDevice, VkShaderModule, const VkAllocationCallbacks*);

DECL_HOOK(VkResult, vkCreatePipelineLayout, VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*);
DECL_HOOK(void, vkDestroyPipelineLayout, VkDevice, VkPipelineLayout, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkCreateGraphicsPipelines, VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
DECL_HOOK(VkResult, vkCreateComputePipelines, VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
DECL_HOOK(void, vkDestroyPipeline, VkDevice, VkPipeline, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkCreatePipelineCache, VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache*);
DECL_HOOK(void, vkDestroyPipelineCache, VkDevice, VkPipelineCache, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkMergePipelineCaches, VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*);

DECL_HOOK(VkResult, vkCreateRenderPass, VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*);
DECL_HOOK(void, vkDestroyRenderPass, VkDevice, VkRenderPass, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkCreateFramebuffer, VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*);
DECL_HOOK(void, vkDestroyFramebuffer, VkDevice, VkFramebuffer, const VkAllocationCallbacks*);

DECL_HOOK(VkResult, vkCreateDescriptorSetLayout, VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*);
DECL_HOOK(void, vkDestroyDescriptorSetLayout, VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkCreateDescriptorPool, VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*);
DECL_HOOK(void, vkDestroyDescriptorPool, VkDevice, VkDescriptorPool, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkAllocateDescriptorSets, VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
DECL_HOOK(VkResult, vkFreeDescriptorSets, VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet*);
DECL_HOOK(void, vkUpdateDescriptorSets, VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const VkCopyDescriptorSet*);
DECL_HOOK(VkResult, vkResetDescriptorPool, VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags);

DECL_HOOK(VkResult, vkCreateFence, VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*);
DECL_HOOK(void, vkDestroyFence, VkDevice, VkFence, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkWaitForFences, VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
DECL_HOOK(VkResult, vkResetFences, VkDevice, uint32_t, const VkFence*);

DECL_HOOK(VkResult, vkCreateSemaphore, VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*);
DECL_HOOK(void, vkDestroySemaphore, VkDevice, VkSemaphore, const VkAllocationCallbacks*);

DECL_HOOK(VkResult, vkCreateEvent, VkDevice, const VkEventCreateInfo*, const VkAllocationCallbacks*, VkEvent*);
DECL_HOOK(void, vkDestroyEvent, VkDevice, VkEvent, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkSetEvent, VkDevice, VkEvent);
DECL_HOOK(VkResult, vkResetEvent, VkDevice, VkEvent);

DECL_HOOK(VkResult, vkCreateQueryPool, VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool*);
DECL_HOOK(void, vkDestroyQueryPool, VkDevice, VkQueryPool, const VkAllocationCallbacks*);

DECL_HOOK(void, vkCmdDraw, VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
DECL_HOOK(void, vkCmdDrawIndexed, VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
DECL_HOOK(void, vkCmdDrawIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
DECL_HOOK(void, vkCmdDrawIndexedIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
DECL_HOOK(void, vkCmdDispatch, VkCommandBuffer, uint32_t, uint32_t, uint32_t);
DECL_HOOK(void, vkCmdDispatchIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize);
DECL_HOOK(void, vkCmdExecuteCommands, VkCommandBuffer, uint32_t, const VkCommandBuffer*);

DECL_HOOK(void, vkCmdBindPipeline, VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
DECL_HOOK(void, vkCmdBindVertexBuffers, VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
DECL_HOOK(void, vkCmdBindIndexBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
DECL_HOOK(void, vkCmdBindDescriptorSets, VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);

DECL_HOOK(void, vkCmdSetViewport, VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
DECL_HOOK(void, vkCmdSetScissor, VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
DECL_HOOK(void, vkCmdPushConstants, VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*);
DECL_HOOK(void, vkCmdSetDepthBias, VkCommandBuffer, float, float, float);
DECL_HOOK(void, vkCmdSetLineWidth, VkCommandBuffer, float);
DECL_HOOK(void, vkCmdSetDepthBounds, VkCommandBuffer, float, float);
DECL_HOOK(void, vkCmdSetStencilCompareMask, VkCommandBuffer, VkStencilFaceFlags, uint32_t);
DECL_HOOK(void, vkCmdSetStencilWriteMask, VkCommandBuffer, VkStencilFaceFlags, uint32_t);
DECL_HOOK(void, vkCmdSetStencilReference, VkCommandBuffer, VkStencilFaceFlags, uint32_t);
DECL_HOOK(void, vkCmdSetBlendConstants, VkCommandBuffer, const float*);

DECL_HOOK(void, vkCmdCopyBuffer, VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*);
DECL_HOOK(void, vkCmdCopyImage, VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy*);
DECL_HOOK(void, vkCmdBlitImage, VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit*, VkFilter);
DECL_HOOK(void, vkCmdCopyBufferToImage, VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy*);
DECL_HOOK(void, vkCmdCopyImageToBuffer, VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy*);
DECL_HOOK(void, vkCmdUpdateBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void*);
DECL_HOOK(void, vkCmdFillBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
DECL_HOOK(void, vkCmdClearColorImage, VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t, const VkImageSubresourceRange*);
DECL_HOOK(void, vkCmdClearDepthStencilImage, VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue*, uint32_t, const VkImageSubresourceRange*);
DECL_HOOK(void, vkCmdClearAttachments, VkCommandBuffer, uint32_t, const VkClearAttachment*, uint32_t, const VkClearRect*);
DECL_HOOK(void, vkCmdResolveImage, VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageResolve*);

DECL_HOOK(void, vkCmdPipelineBarrier, VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*);
DECL_HOOK(void, vkCmdBeginRenderPass, VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents);
DECL_HOOK(void, vkCmdEndRenderPass, VkCommandBuffer);
DECL_HOOK(void, vkCmdNextSubpass, VkCommandBuffer, VkSubpassContents);

DECL_HOOK(void, vkCmdSetEvent, VkCommandBuffer, VkEvent, VkPipelineStageFlags);
DECL_HOOK(void, vkCmdResetEvent, VkCommandBuffer, VkEvent, VkPipelineStageFlags);
DECL_HOOK(void, vkCmdWaitEvents, VkCommandBuffer, uint32_t, const VkEvent*, VkPipelineStageFlags, VkPipelineStageFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*);

DECL_HOOK(void, vkCmdBeginQuery, VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags);
DECL_HOOK(void, vkCmdEndQuery, VkCommandBuffer, VkQueryPool, uint32_t);
DECL_HOOK(void, vkCmdWriteTimestamp, VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t);
DECL_HOOK(void, vkCmdResetQueryPool, VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
DECL_HOOK(void, vkCmdCopyQueryPoolResults, VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags);

// ---- Vulkan 1.1 ----
DECL_HOOK(VkResult, vkBindBufferMemory2, VkDevice, uint32_t, const VkBindBufferMemoryInfo*);
DECL_HOOK(VkResult, vkBindImageMemory2, VkDevice, uint32_t, const VkBindImageMemoryInfo*);
DECL_HOOK(void, vkTrimCommandPool, VkDevice, VkCommandPool, VkCommandPoolTrimFlags);

// ---- Vulkan 1.2 ----
DECL_HOOK(void, vkCmdDrawIndirectCount, VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
DECL_HOOK(void, vkCmdDrawIndexedIndirectCount, VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
DECL_HOOK(VkResult, vkCreateRenderPass2, VkDevice, const VkRenderPassCreateInfo2*, const VkAllocationCallbacks*, VkRenderPass*);
DECL_HOOK(void, vkCmdBeginRenderPass2, VkCommandBuffer, const VkRenderPassBeginInfo*, const VkSubpassBeginInfo*);
DECL_HOOK(void, vkCmdNextSubpass2, VkCommandBuffer, const VkSubpassBeginInfo*, const VkSubpassEndInfo*);
DECL_HOOK(void, vkCmdEndRenderPass2, VkCommandBuffer, const VkSubpassEndInfo*);
DECL_HOOK(void, vkResetQueryPool, VkDevice, VkQueryPool, uint32_t, uint32_t);
DECL_HOOK(VkResult, vkGetSemaphoreCounterValue, VkDevice, VkSemaphore, uint64_t*);
DECL_HOOK(VkResult, vkWaitSemaphores, VkDevice, const VkSemaphoreWaitInfo*, uint64_t);
DECL_HOOK(VkResult, vkSignalSemaphore, VkDevice, const VkSemaphoreSignalInfo*);

// ---- Vulkan 1.3 — Dynamic Rendering ----
DECL_HOOK(void, vkCmdBeginRendering, VkCommandBuffer, const VkRenderingInfo*);
DECL_HOOK(void, vkCmdEndRendering, VkCommandBuffer);

// ---- Vulkan 1.3 — Extended Dynamic State ----
DECL_HOOK(void, vkCmdSetCullMode, VkCommandBuffer, VkCullModeFlags);
DECL_HOOK(void, vkCmdSetFrontFace, VkCommandBuffer, VkFrontFace);
DECL_HOOK(void, vkCmdSetPrimitiveTopology, VkCommandBuffer, VkPrimitiveTopology);
DECL_HOOK(void, vkCmdSetViewportWithCount, VkCommandBuffer, uint32_t, const VkViewport*);
DECL_HOOK(void, vkCmdSetScissorWithCount, VkCommandBuffer, uint32_t, const VkRect2D*);
DECL_HOOK(void, vkCmdSetDepthTestEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetDepthWriteEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetDepthCompareOp, VkCommandBuffer, VkCompareOp);
DECL_HOOK(void, vkCmdSetDepthBoundsTestEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetStencilTestEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetStencilOp, VkCommandBuffer, VkStencilFaceFlags, VkStencilOp, VkStencilOp, VkStencilOp, VkCompareOp);
DECL_HOOK(void, vkCmdSetRasterizerDiscardEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetDepthBiasEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetPrimitiveRestartEnable, VkCommandBuffer, VkBool32);
DECL_HOOK(void, vkCmdSetVertexInputEXT, VkCommandBuffer, uint32_t, const VkVertexInputBindingDescription2EXT*, uint32_t, const VkVertexInputAttributeDescription2EXT*);

// ---- Vulkan 1.3 — Synchronization2 ----
DECL_HOOK(void, vkCmdPipelineBarrier2, VkCommandBuffer, const VkDependencyInfo*);
DECL_HOOK(void, vkCmdResetEvent2, VkCommandBuffer, VkEvent, VkPipelineStageFlags2);
DECL_HOOK(void, vkCmdSetEvent2, VkCommandBuffer, VkEvent, const VkDependencyInfo*);
DECL_HOOK(void, vkCmdWaitEvents2, VkCommandBuffer, uint32_t, const VkEvent*, const VkDependencyInfo*);
DECL_HOOK(void, vkCmdWriteTimestamp2, VkCommandBuffer, VkPipelineStageFlags2, VkQueryPool, uint32_t);
DECL_HOOK(VkResult, vkQueueSubmit2, VkQueue, uint32_t, const VkSubmitInfo2*, VkFence);

// ---- Vulkan 1.3 — Copy Commands 2 ----
DECL_HOOK(void, vkCmdCopyBuffer2, VkCommandBuffer, const VkCopyBufferInfo2*);
DECL_HOOK(void, vkCmdCopyImage2, VkCommandBuffer, const VkCopyImageInfo2*);
DECL_HOOK(void, vkCmdCopyBufferToImage2, VkCommandBuffer, const VkCopyBufferToImageInfo2*);
DECL_HOOK(void, vkCmdCopyImageToBuffer2, VkCommandBuffer, const VkCopyImageToBufferInfo2*);
DECL_HOOK(void, vkCmdBlitImage2, VkCommandBuffer, const VkBlitImageInfo2*);
DECL_HOOK(void, vkCmdResolveImage2, VkCommandBuffer, const VkResolveImageInfo2*);

// ---- Vulkan 1.3 — Private Data ----
DECL_HOOK(VkResult, vkCreatePrivateDataSlotEXT, VkDevice, const VkPrivateDataSlotCreateInfo*, const VkAllocationCallbacks*, VkPrivateDataSlot*);
DECL_HOOK(void, vkDestroyPrivateDataSlotEXT, VkDevice, VkPrivateDataSlot, const VkAllocationCallbacks*);
DECL_HOOK(VkResult, vkSetPrivateDataEXT, VkDevice, VkObjectType, uint64_t, VkPrivateDataSlot, uint64_t);

// ---- Manual hooks (cached GPU capabilities / guest-side queries) ----

// Instance / Device lifecycle
VkResult VKAPI_PTR vkCreateInstance_hook(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
void     VKAPI_PTR vkDestroyInstance_hook(VkInstance, const VkAllocationCallbacks*);
VkResult VKAPI_PTR vkEnumeratePhysicalDevices_hook(VkInstance, uint32_t*, VkPhysicalDevice*);
VkResult VKAPI_PTR vkCreateDevice_hook(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
void     VKAPI_PTR vkDestroyDevice_hook(VkDevice, const VkAllocationCallbacks*);

// Queue
void     VKAPI_PTR vkGetDeviceQueue_hook(VkDevice, uint32_t, uint32_t, VkQueue*);
void     VKAPI_PTR vkGetDeviceQueue2_hook(VkDevice, const VkDeviceQueueInfo2*, VkQueue*);

// Proc addr
PFN_vkVoidFunction VKAPI_PTR vkGetInstanceProcAddr_hook(VkInstance, const char*);
PFN_vkVoidFunction VKAPI_PTR vkGetDeviceProcAddr_hook(VkDevice, const char*);

// Enumeration
VkResult VKAPI_PTR vkEnumerateInstanceExtensionProperties_hook(const char*, uint32_t*, VkExtensionProperties*);
VkResult VKAPI_PTR vkEnumerateDeviceExtensionProperties_hook(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
VkResult VKAPI_PTR vkEnumerateInstanceLayerProperties_hook(uint32_t*, VkLayerProperties*);
VkResult VKAPI_PTR vkEnumerateDeviceLayerProperties_hook(VkPhysicalDevice, uint32_t*, VkLayerProperties*);
VkResult VKAPI_PTR vkEnumerateInstanceVersion_hook(uint32_t*);
VkResult VKAPI_PTR vkEnumeratePhysicalDeviceGroups_hook(VkInstance, uint32_t*, VkPhysicalDeviceGroupProperties*);

// Physical device properties / features / memory (1.0)
void VKAPI_PTR vkGetPhysicalDeviceProperties_hook(VkPhysicalDevice, VkPhysicalDeviceProperties*);
void VKAPI_PTR vkGetPhysicalDeviceProperties2_hook(VkPhysicalDevice, VkPhysicalDeviceProperties2*);
void VKAPI_PTR vkGetPhysicalDeviceFeatures_hook(VkPhysicalDevice, VkPhysicalDeviceFeatures*);
void VKAPI_PTR vkGetPhysicalDeviceFeatures2_hook(VkPhysicalDevice, VkPhysicalDeviceFeatures2*);
void VKAPI_PTR vkGetPhysicalDeviceMemoryProperties_hook(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
void VKAPI_PTR vkGetPhysicalDeviceMemoryProperties2_hook(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2*);
void VKAPI_PTR vkGetPhysicalDeviceQueueFamilyProperties_hook(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
void VKAPI_PTR vkGetPhysicalDeviceQueueFamilyProperties2_hook(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties2*);
void VKAPI_PTR vkGetPhysicalDeviceFormatProperties_hook(VkPhysicalDevice, VkFormat, VkFormatProperties*);
void VKAPI_PTR vkGetPhysicalDeviceFormatProperties2_hook(VkPhysicalDevice, VkFormat, VkFormatProperties2*);
VkResult VKAPI_PTR vkGetPhysicalDeviceImageFormatProperties_hook(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties*);
VkResult VKAPI_PTR vkGetPhysicalDeviceImageFormatProperties2_hook(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2*, VkImageFormatProperties2*);
void VKAPI_PTR vkGetPhysicalDeviceSparseImageFormatProperties_hook(VkPhysicalDevice, VkFormat, VkImageType, VkSampleCountFlagBits, VkImageUsageFlags, VkImageTiling, uint32_t*, VkSparseImageFormatProperties*);
void VKAPI_PTR vkGetPhysicalDeviceSparseImageFormatProperties2_hook(VkPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2*, uint32_t*, VkSparseImageFormatProperties2*);

// Physical device external properties (1.1)
void VKAPI_PTR vkGetPhysicalDeviceExternalBufferProperties_hook(VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo*, VkExternalBufferProperties*);
void VKAPI_PTR vkGetPhysicalDeviceExternalFenceProperties_hook(VkPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo*, VkExternalFenceProperties*);
void VKAPI_PTR vkGetPhysicalDeviceExternalSemaphoreProperties_hook(VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo*, VkExternalSemaphoreProperties*);

// Misc queries (1.1+)
void     VKAPI_PTR vkGetDescriptorSetLayoutSupport_hook(VkDevice, const VkDescriptorSetLayoutCreateInfo*, VkDescriptorSetLayoutSupport*);
uint64_t VKAPI_PTR vkGetBufferDeviceAddress_hook(VkDevice, const VkBufferDeviceAddressInfo*);
uint64_t VKAPI_PTR vkGetBufferOpaqueCaptureAddress_hook(VkDevice, const VkBufferDeviceAddressInfo*);
uint64_t VKAPI_PTR vkGetDeviceMemoryOpaqueCaptureAddress_hook(VkDevice, const VkDeviceMemoryOpaqueCaptureAddressInfo*);

// Query / Status
VkResult VKAPI_PTR vkGetFenceStatus_hook(VkDevice, VkFence);
VkResult VKAPI_PTR vkGetEventStatus_hook(VkDevice, VkEvent);
VkResult VKAPI_PTR vkGetPrivateData_hook(VkDevice, VkObjectType, uint64_t, VkPrivateDataSlot, uint64_t*);
void     VKAPI_PTR vkGetRenderAreaGranularity_hook(VkDevice, VkRenderPass, VkExtent2D*);
void     VKAPI_PTR vkGetImageSubresourceLayout_hook(VkDevice, VkImage, const VkImageSubresource*, VkSubresourceLayout*);
VkResult VKAPI_PTR vkGetQueryPoolResults_hook(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, VkQueryResultFlags);
VkResult VKAPI_PTR vkGetPipelineCacheData_hook(VkDevice, VkPipelineCache, size_t*, void*);

// KHR
VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceSupportKHR_hook(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*);

// Tool properties (1.3)
VkResult VKAPI_PTR vkGetPhysicalDeviceToolPropertiesEXT_hook(VkPhysicalDevice, uint32_t*, VkPhysicalDeviceToolProperties*);

// Register a manual hook (implemented in generated vk_intercept_gen.cpp)
void register_manual_hook(const char* name, void* func);

} // namespace omnigpu::intercept
