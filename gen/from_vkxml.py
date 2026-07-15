#!/usr/bin/env python3
"""
Generate vulkan_api.json from the authoritative vk.xml registry.

Usage:
    python gen/from_vkxml.py --xml third_party/Vulkan-Headers/registry/vk.xml --output gen/vulkan_api.json
"""

import argparse
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Optional


# Core version features and their command counts
CORE_VERSION_FEATURES = {
    "VK_BASE_VERSION_1_0": "1.0",
    "VK_COMPUTE_VERSION_1_0": "1.0",
    "VK_GRAPHICS_VERSION_1_0": "1.0",
    "VK_BASE_VERSION_1_1": "1.1",
    "VK_COMPUTE_VERSION_1_1": "1.1",
    "VK_GRAPHICS_VERSION_1_1": "1.1",
    "VK_BASE_VERSION_1_2": "1.2",
    "VK_COMPUTE_VERSION_1_2": "1.2",
    "VK_GRAPHICS_VERSION_1_2": "1.2",
    "VK_BASE_VERSION_1_3": "1.3",
    "VK_COMPUTE_VERSION_1_3": "1.3",
    "VK_GRAPHICS_VERSION_1_3": "1.3",
}

# Functions with manual implementations – excluded from auto-generation
MANUAL_FUNCTIONS = {
    # Instance / Device / Enum
    "vkCreateInstance", "vkDestroyInstance",
    "vkEnumeratePhysicalDevices",
    "vkCreateDevice", "vkDestroyDevice",
    "vkGetDeviceQueue", "vkGetDeviceQueue2",
    "vkGetInstanceProcAddr", "vkGetDeviceProcAddr",
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateDeviceExtensionProperties",
    "vkEnumerateInstanceLayerProperties",
    "vkEnumerateDeviceLayerProperties",
    "vkEnumerateInstanceVersion",
    "vkEnumeratePhysicalDeviceGroups",
    # Physical device queries (1.0)
    "vkGetPhysicalDeviceProperties",
    "vkGetPhysicalDeviceProperties2",
    "vkGetPhysicalDeviceFeatures",
    "vkGetPhysicalDeviceFeatures2",
    "vkGetPhysicalDeviceMemoryProperties",
    "vkGetPhysicalDeviceMemoryProperties2",
    "vkGetPhysicalDeviceQueueFamilyProperties",
    "vkGetPhysicalDeviceQueueFamilyProperties2",
    "vkGetPhysicalDeviceFormatProperties",
    "vkGetPhysicalDeviceFormatProperties2",
    "vkGetPhysicalDeviceImageFormatProperties",
    "vkGetPhysicalDeviceImageFormatProperties2",
    "vkGetPhysicalDeviceSparseImageFormatProperties",
    "vkGetPhysicalDeviceSparseImageFormatProperties2",
    # Physical device queries (1.1)
    "vkGetPhysicalDeviceExternalBufferProperties",
    "vkGetPhysicalDeviceExternalFenceProperties",
    "vkGetPhysicalDeviceExternalSemaphoreProperties",
    "vkGetPhysicalDeviceToolProperties",
    "vkGetPhysicalDeviceToolPropertiesEXT",
    # Device queries
    "vkGetBufferMemoryRequirements",
    "vkGetBufferMemoryRequirements2",
    "vkGetImageMemoryRequirements",
    "vkGetImageMemoryRequirements2",
    "vkGetImageSparseMemoryRequirements",
    "vkGetImageSparseMemoryRequirements2",
    "vkGetDeviceMemoryCommitment",
    "vkGetFenceStatus",
    "vkGetEventStatus",
    "vkGetRenderAreaGranularity",
    "vkGetImageSubresourceLayout",
    "vkGetQueryPoolResults",
    "vkGetPipelineCacheData",
    "vkGetDescriptorSetLayoutSupport",
    "vkGetBufferDeviceAddress",
    "vkGetBufferOpaqueCaptureAddress",
    "vkGetDeviceMemoryOpaqueCaptureAddress",
    # KHR surface
    "vkGetPhysicalDeviceSurfaceSupportKHR",
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
    "vkGetPhysicalDeviceSurfaceFormatsKHR",
    "vkGetPhysicalDeviceSurfacePresentModesKHR",
    "vkGetPhysicalDevicePresentRectanglesKHR",
    # KHR display
    "vkGetPhysicalDeviceDisplayPropertiesKHR",
    "vkGetPhysicalDeviceDisplayPlanePropertiesKHR",
    "vkGetDisplayPlaneSupportedDisplaysKHR",
    "vkGetDisplayModePropertiesKHR",
    "vkGetDisplayPlaneCapabilitiesKHR",
    "vkCreateDisplayModeKHR",
    "vkCreateDisplayPlaneSurfaceKHR",
    "vkDestroySurfaceKHR",
}

# Types that are handles
HANDLE_TYPES = set()

# Types that are structs (for struct_ptr detection)
STRUCT_TYPES = set()

# Types that are enums / bitmasks
ENUM_TYPES = set()


def load_type_info(xml_root):
    """Load type classification info from the xml registry."""
    ENUM_TYPES.update({
        "VkBool32", "VkResult", "VkFormat",
    })
    for child in xml_root.findall(".//types/type"):
        cat = child.get("category", "")
        name_elem = child.find("name")
        if name_elem is None:
            continue
        name = name_elem.text or ""
        if cat == "handle":
            HANDLE_TYPES.add(name)
        elif cat == "enum":
            ENUM_TYPES.add(name)
        elif cat == "bitmask":
            ENUM_TYPES.add(name)
            flag_bits = name.replace("Flags", "FlagBits")
            ENUM_TYPES.add(flag_bits)
        elif cat in ("struct", "union"):
            STRUCT_TYPES.add(name)

    # Add all well-known handles
    for h in [
        "VkInstance", "VkPhysicalDevice", "VkDevice", "VkQueue",
        "VkCommandBuffer", "VkCommandPool",
        "VkBuffer", "VkBufferView",
        "VkImage", "VkImageView",
        "VkDeviceMemory",
        "VkShaderModule",
        "VkPipeline", "VkPipelineLayout", "VkPipelineCache",
        "VkRenderPass", "VkFramebuffer",
        "VkDescriptorSetLayout", "VkDescriptorPool", "VkDescriptorSet",
        "VkSampler",
        "VkSemaphore", "VkFence", "VkEvent",
        "VkQueryPool",
        "VkSurfaceKHR", "VkSwapchainKHR",
        "VkDisplayKHR", "VkDisplayModeKHR",
        "VkPrivateDataSlot",
        "VkDescriptorUpdateTemplate", "VkSamplerYcbcrConversion",
        "VkDebugReportCallbackEXT", "VkDebugUtilsMessengerEXT",
        "VkValidationCacheEXT", "VkPerformanceConfigurationINTEL",
        "VkAccelerationStructureKHR", "VkAccelerationStructureNV",
        "VkIndirectCommandsLayoutNV", "VkDeferredOperationKHR",
    ]:
        HANDLE_TYPES.add(h)

    # Add standard Vulkan enums / bitmasks
    for e in [
        "VkImageLayout", "VkImageType", "VkImageTiling",
        "VkImageUsageFlagBits", "VkImageUsageFlags",
        "VkImageCreateFlagBits", "VkImageCreateFlags",
        "VkSampleCountFlagBits", "VkSampleCountFlags",
        "VkFilter", "VkSamplerAddressMode", "VkSamplerMipmapMode",
        "VkBorderColor", "VkCompareOp", "VkStencilOp",
        "VkStencilFaceFlagBits", "VkStencilFaceFlags",
        "VkBlendFactor", "VkBlendOp", "VkColorComponentFlagBits",
        "VkColorComponentFlags", "VkLogicOp", "VkFrontFace",
        "VkCullModeFlagBits", "VkCullModeFlags",
        "VkPrimitiveTopology", "VkPolygonMode",
        "VkPipelineBindPoint", "VkShaderStageFlagBits", "VkShaderStageFlags",
        "VkPipelineStageFlagBits", "VkPipelineStageFlags",
        "VkPipelineStageFlagBits2", "VkPipelineStageFlags2",
        "VkAccessFlagBits", "VkAccessFlags",
        "VkAccessFlagBits2", "VkAccessFlags2",
        "VkDependencyFlagBits", "VkDependencyFlags",
        "VkSubpassContents",
        "VkQueryType", "VkQueryControlFlagBits", "VkQueryControlFlags",
        "VkQueryResultFlagBits", "VkQueryResultFlags",
        "VkQueryPipelineStatisticFlagBits", "VkQueryPipelineStatisticFlags",
        "VkMemoryHeapFlagBits", "VkMemoryHeapFlags",
        "VkMemoryPropertyFlagBits", "VkMemoryPropertyFlags",
        "VkBufferUsageFlagBits", "VkBufferUsageFlags",
        "VkBufferCreateFlagBits", "VkBufferCreateFlags",
        "VkCommandBufferUsageFlagBits", "VkCommandBufferUsageFlags",
        "VkCommandBufferLevel",
        "VkCommandPoolCreateFlagBits", "VkCommandPoolCreateFlags",
        "VkCommandPoolResetFlagBits", "VkCommandPoolResetFlags",
        "VkCommandPoolTrimFlags",
        "VkIndexType", "VkDescriptorType",
        "VkDescriptorPoolCreateFlagBits", "VkDescriptorPoolCreateFlags",
        "VkDescriptorPoolResetFlags",
        "VkDescriptorSetLayoutCreateFlagBits", "VkDescriptorSetLayoutCreateFlags",
        "VkVertexInputRate",
        "VkSharingMode",
        "VkAttachmentLoadOp", "VkAttachmentStoreOp",
        "VkPipelineCreateFlagBits", "VkPipelineCreateFlags",
        "VkShaderModuleCreateFlagBits",
        "VkFenceCreateFlagBits", "VkFenceCreateFlags",
        "VkSemaphoreCreateFlagBits", "VkSemaphoreType",
        "VkEventCreateFlagBits", "VkEventCreateFlags",
        "VkPresentModeKHR", "VkColorSpaceKHR",
        "VkCompositeAlphaFlagBitsKHR", "VkCompositeAlphaFlagsKHR",
        "VkSurfaceTransformFlagBitsKHR", "VkSurfaceTransformFlagsKHR",
        "VkSwapchainCreateFlagBitsKHR", "VkSwapchainCreateFlagsKHR",
        "VkObjectType", "VkSystemAllocationScope",
        "VkInternalAllocationType",
        "VkPointClippingBehavior",
        "VkResolveModeFlagBits",
        "VkDescriptorUpdateTemplateType",
        "VkExternalMemoryHandleTypeFlagBits", "VkExternalMemoryFeatureFlagBits",
        "VkExternalFenceHandleTypeFlagBits", "VkExternalFenceFeatureFlagBits",
        "VkExternalSemaphoreHandleTypeFlagBits", "VkExternalSemaphoreFeatureFlagBits",
        "VkSemaphoreWaitFlagBits", "VkSemaphoreWaitFlags",
        "VkRenderingFlagBits", "VkRenderingFlags",
        "VkMemoryMapFlags",
        "VkPeerMemoryFeatureFlags",
        "VkDeviceDiagnosticsConfigFlagsNV",
        "VkDeviceGroupPresentModeFlagBitsKHR", "VkDeviceGroupPresentModeFlagsKHR",
    ]:
        ENUM_TYPES.add(e)

    # Add all core struct types programmatically from XML
    for child in xml_root.findall(".//types/type"):
        cat = child.get("category", "")
        name_elem = child.find("name")
        if name_elem is None:
            continue
        name = name_elem.text or ""
        if cat in ("struct", "union") and name.startswith("Vk"):
            STRUCT_TYPES.add(name)

    # Additional well-known Vulkan 1.x structs not categorized in XML
    for s in [
        "VkSubmitInfo", "VkSubmitInfo2", "VkPresentInfoKHR",
        "VkCommandBufferAllocateInfo", "VkCommandBufferBeginInfo",
        "VkMemoryAllocateInfo", "VkMappedMemoryRange",
        "VkBufferCreateInfo", "VkImageCreateInfo", "VkImageViewCreateInfo",
        "VkSamplerCreateInfo",
        "VkShaderModuleCreateInfo",
        "VkPipelineLayoutCreateInfo", "VkPipelineShaderStageCreateInfo",
        "VkGraphicsPipelineCreateInfo", "VkComputePipelineCreateInfo",
        "VkPipelineCacheCreateInfo",
        "VkRenderPassCreateInfo", "VkRenderPassCreateInfo2",
        "VkFramebufferCreateInfo",
        "VkDescriptorSetLayoutCreateInfo", "VkDescriptorPoolCreateInfo",
        "VkDescriptorSetAllocateInfo",
        "VkFenceCreateInfo", "VkSemaphoreCreateInfo",
        "VkEventCreateInfo", "VkQueryPoolCreateInfo",
        "VkCommandPoolCreateInfo",
        "VkRenderingInfo", "VkDependencyInfo",
        "VkCopyBufferInfo2", "VkCopyImageInfo2",
        "VkCopyBufferToImageInfo2", "VkCopyImageToBufferInfo2",
        "VkBlitImageInfo2", "VkResolveImageInfo2",
        "VkDeviceQueueInfo2",
        "VkPhysicalDeviceExternalBufferInfo", "VkPhysicalDeviceExternalFenceInfo",
        "VkPhysicalDeviceExternalSemaphoreInfo",
        "VkExternalBufferProperties", "VkExternalFenceProperties",
        "VkExternalSemaphoreProperties",
        "VkBufferDeviceAddressInfo", "VkBufferDeviceCaptureAddressInfo",
        "VkDeviceMemoryOpaqueCaptureAddressInfo",
        "VkSemaphoreWaitInfo", "VkSemaphoreSignalInfo",
        "VkPrivateDataSlotCreateInfo",
        "VkPhysicalDeviceImageFormatInfo2", "VkImageFormatProperties2",
        "VkPhysicalDeviceSparseImageFormatInfo2",
        "VkSparseImageFormatProperties2",
        "VkDescriptorSetLayoutSupport",
        "VkVertexInputBindingDescription2EXT",
        "VkVertexInputAttributeDescription2EXT",
        "VkInstanceCreateInfo", "VkDeviceCreateInfo",
        "VkSwapchainCreateInfoKHR",
        "VkAllocationCallbacks",
        "VkSubpassBeginInfo", "VkSubpassEndInfo",
        "VkPhysicalDeviceToolProperties",
        "VkDeviceGroupPresentCapabilitiesKHR",
        "VkImageSubresource", "VkSubresourceLayout",
        "VkDisplayModeCreateInfoKHR", "VkDisplaySurfaceCreateInfoKHR",
        "VkSurfaceCapabilitiesKHR", "VkSurfaceFormatKHR",
        "VkAcquireNextImageInfoKHR",
        "VkBufferCopy", "VkBufferImageCopy", "VkImageCopy",
        "VkImageBlit", "VkImageResolve",
        "VkBufferMemoryBarrier", "VkImageMemoryBarrier", "VkMemoryBarrier",
        "VkClearColorValue", "VkClearDepthStencilValue",
        "VkClearAttachment", "VkClearRect",
        "VkImageSubresourceRange", "VkImageSubresourceLayers",
        "VkWriteDescriptorSet", "VkCopyDescriptorSet",
        "VkViewport", "VkRect2D",
        "VkVertexInputBindingDescription", "VkVertexInputAttributeDescription",
        "VkPipelineVertexInputStateCreateInfo",
        "VkPipelineInputAssemblyStateCreateInfo",
        "VkPipelineTessellationStateCreateInfo",
        "VkPipelineViewportStateCreateInfo",
        "VkPipelineRasterizationStateCreateInfo",
        "VkPipelineMultisampleStateCreateInfo",
        "VkPipelineDepthStencilStateCreateInfo",
        "VkPipelineColorBlendStateCreateInfo",
        "VkPipelineDynamicStateCreateInfo",
        "VkSpecializationInfo", "VkSpecializationMapEntry",
        "VkPushConstantRange", "VkDescriptorSetLayoutBinding",
        "VkDescriptorPoolSize",
        "VkStencilOpState",
        "VkPipelineColorBlendAttachmentState",
        "VkRenderPassBeginInfo",
        "VkDeviceQueueCreateInfo",
        "VkExtensionProperties", "VkLayerProperties",
        "VkPhysicalDeviceProperties",
        "VkPhysicalDeviceProperties2",
        "VkPhysicalDeviceFeatures",
        "VkPhysicalDeviceFeatures2",
        "VkPhysicalDeviceMemoryProperties",
        "VkPhysicalDeviceMemoryProperties2",
        "VkPhysicalDeviceVulkan11Properties",
        "VkPhysicalDeviceVulkan12Properties",
        "VkPhysicalDeviceVulkan13Properties",
        "VkPhysicalDeviceVulkan11Features",
        "VkPhysicalDeviceVulkan12Features",
        "VkPhysicalDeviceVulkan13Features",
        "VkMemoryRequirements", "VkMemoryRequirements2",
        "VkPhysicalDeviceGroupProperties",
        "VkFormatProperties", "VkFormatProperties2",
        "VkQueueFamilyProperties", "VkQueueFamilyProperties2",
        "VkImageFormatProperties", "VkImageFormatProperties2",
        "VkSparseImageFormatProperties",
        "VkPhysicalDeviceSparseImageFormatInfo2",
        "VkSparseImageFormatProperties2",
        "VkPhysicalDevicePushDescriptorPropertiesKHR",
        "VkDisplayPropertiesKHR", "VkDisplayPlanePropertiesKHR",
        "VkDisplayModePropertiesKHR", "VkDisplayPlaneCapabilitiesKHR",
        "VkBindBufferMemoryInfo", "VkBindImageMemoryInfo",
        "VkBindSparseInfo",
        "VkDescriptorUpdateTemplateCreateInfo",
        "VkSamplerYcbcrConversionCreateInfo",
        "VkSamplerYcbcrConversionInfo",
        "VkBufferViewCreateInfo",
        "VkDeviceBufferMemoryRequirements",
        "VkDeviceImageMemoryRequirements",
    ]:
        STRUCT_TYPES.add(s)


def reconstruct_param_type(param) -> str:
    """Reconstruct the full type string of a param from its XML elements."""
    parts = []
    if param.text:
        parts.append(param.text)
    for child in param:
        if child.tag == "type":
            parts.append(child.text or "")
        if child.tail:
            parts.append(child.tail)
    full = "".join(parts).strip()
    full = re.sub(r'\s+', ' ', full)
    return full


def extract_param_name_and_type(param) -> tuple[str, str]:
    """Extract parameter name and full type string."""
    name_elem = param.find("name")
    if name_elem is None:
        return "", ""
    name = name_elem.text or ""

    # Build type string from all parts before the name element
    parts = []
    if param.text:
        parts.append(param.text)
    for child in param:
        if child is name_elem:
            break
        if child.tag == "type":
            parts.append(child.text or "")
        if child.tail:
            # Only include tail text up to the name
            idx = child.tail.find(name)
            if idx >= 0:
                parts.append(child.tail[:idx])
                break
            parts.append(child.tail)
        elif child is not name_elem:
            parts.append(child.tail or "")

    raw_type = "".join(parts)
    raw_type = re.sub(r'\s+', ' ', raw_type).strip()
    return name, raw_type


def extract_command_details(xml_root, cmds: set[str]) -> dict[str, dict]:
    """Extract full parameter details for each command."""
    cmd_map = {}
    cmds_node = xml_root.find(".//commands")
    if cmds_node is None:
        return cmd_map

    for cmd_elem in cmds_node.findall("command"):
        proto = cmd_elem.find("proto")
        if proto is None:
            continue  # skip aliases
        name_elem = proto.find("name")
        if name_elem is None:
            continue
        func_name = name_elem.text or ""
        if func_name not in cmds:
            continue

        # Build return type
        ret_parts = []
        if proto.text:
            ret_parts.append(proto.text)
        for child in proto:
            if child.tag == "type":
                ret_parts.append(child.text or "")
            if child is name_elem:
                break
            if child.tail:
                ret_parts.append(child.tail)
        ret_type = "".join(ret_parts).strip()
        ret_type = re.sub(r'\s+', ' ', ret_type)

        # Extract params, deduplicating by name
        seen_names = set()
        params = []
        for param in cmd_elem.findall("param"):
            pname, ptype = extract_param_name_and_type(param)
            if not pname or pname in seen_names:
                continue
            seen_names.add(pname)
            params.append({
                "name": pname,
                "type": ptype,
                "len": param.get("len", ""),
            })

        cmd_map[func_name] = {
            "return_type": ret_type,
            "params": params,
        }
    return cmd_map


def is_const_ptr(ptype: str) -> bool:
    return ptype.startswith("const ") and ptype.endswith("*")


def get_previous_count_param(pname: str, all_params: list[dict]) -> Optional[str]:
    """Find the nearest uint32_t count param preceding pname."""
    param_idx = next((i for i, p in enumerate(all_params) if p["name"] == pname), -1)
    for j in range(param_idx - 1, -1, -1):
        prev = all_params[j]
        if prev.get("kind") == "value" and get_pure_type(prev["type"]) == "uint32_t":
            pn = prev["name"].lower()
            if ("count" in pn or "size" in pn):
                return prev["name"]
        # Don't break - keep looking past non-count uint32_t like firstSet
    return None


def classify_kind(ptype: str, pname: str, func_name: str, all_params: list[dict]) -> str:
    """Determine serialisation kind for a parameter."""
    base = ptype.replace("const ", "").strip()
    is_ptr = base.endswith("*")
    core_type = base.replace("*", "").strip()

    # Special overrides
    special_overrides = [
        # const float* blendConstants with 16 bytes (4 floats)
        ("vkCmdSetBlendConstants", "blendConstants"),
        # void* pData parameters with size param
    ]
    if (func_name, pname) in special_overrides:
        return "raw_ptr"

    # ... rest of function ...

    # Special overrides
    overrides = {
        ("vkCmdPushConstants", "pValues"): "raw_ptr",
        ("vkCmdUpdateBuffer", "pData"): "raw_ptr",
        ("vkMapMemory", "ppData"): "value_ptr",
    }
    key = (func_name, pname)
    if key in overrides:
        return overrides[key]

    # Double pointer
    if ptype.count("*") == 2 and not ptype.startswith("const"):
        return "value_ptr"

    # Array of handles detection: const handle* + preceding count param = input array
    if is_const_ptr(ptype) and core_type in HANDLE_TYPES:
        count_param = get_previous_count_param(pname, all_params)
        if count_param:
            return "array"

    # Handle pointers (output handles)
    if is_ptr and core_type in HANDLE_TYPES:
        return "output_handle_ptr"

    # Array detection: pointer preceded by a uint32_t count param
    if is_ptr and not ptype.endswith("**"):
        # Exclude VkAllocationCallbacks* from array detection
        if core_type == "VkAllocationCallbacks" or "AllocationCallbacks" in core_type:
            return "struct_ptr"
        count_param = get_previous_count_param(pname, all_params)
        if count_param:
            return "array"

    # Handle (non-pointer)
    if core_type in HANDLE_TYPES:
        return "handle"

    # String
    if "char" == get_pure_type(ptype) and "*" in ptype:
        return "string"

    # Raw pointer
    if is_ptr and core_type == "void":
        return "raw_ptr"

    # Struct pointer
    if is_ptr and core_type in STRUCT_TYPES:
        return "struct_ptr"

    # Struct pointer (heuristic: starts with Vk, not handle)
    if is_ptr and core_type.startswith("Vk") and core_type not in HANDLE_TYPES:
        return "struct_ptr"

    # Value ptr (pointer to enum/int/float)
    if is_ptr:
        return "value_ptr"

    # 64-bit integers
    if core_type in ("VkDeviceSize", "uint64_t", "VkDeviceAddress"):
        return "value"

    # Float
    if core_type in ("float", "double"):
        return "value"

    # Bool
    if core_type == "VkBool32":
        return "value"

    # Default: uint32-compatible value
    if core_type in ("uint32_t", "int32_t", "int", "uint16_t", "int16_t", "uint8_t", "int8_t", "size_t", "ssize_t"):
        return "value"

    # Enums and flags (Vk*)
    if core_type.startswith("Vk") or core_type.endswith("Flags") or core_type.endswith("FlagBits"):
        return "value"

    return "value"


def get_pure_type(ptype: str) -> str:
    """Get the base type name without const/pointer/array qualifiers."""
    t = ptype.replace("const ", "").strip()
    t = re.sub(r'\s*\*+$', '', t)
    t = re.sub(r'\[.*?\]', '', t)
    return t.strip()


def classify_params(func_name: str, raw_params: list[dict]) -> list[dict]:
    """Classify each parameter with its serialisation kind."""
    # First pass: determine initial kinds
    for p in raw_params:
        if "kind" not in p:
            p["kind"] = classify_kind(p["type"], p["name"], func_name, raw_params)

    # Second pass: handle raw_ptr special cases
    for i, p in enumerate(raw_params):
        if p["kind"] == "raw_ptr":
            # Special: vkCmdSetBlendConstants blendConstants = 4 floats
            if func_name == "vkCmdSetBlendConstants" and p["name"] == "blendConstants":
                p["byte_size"] = 16

    # Third pass: handle VkBool32* -> value_ptr
    for p in raw_params:
        core = get_pure_type(p["type"])
        if p["type"].endswith("*") and core == "VkBool32":
            p["kind"] = "value_ptr"

    # Build result
    result = []
    for p in raw_params:
        entry = {
            "type": p["type"],
            "name": p["name"],
            "kind": p["kind"],
        }
        if hasattr(p, "byte_size") or "byte_size" in p:
            entry["byte_size"] = p["byte_size"]

        result.append(entry)
    return result


def extract_feature_commands(xml_root, feature_name: str) -> set[str]:
    """Extract commands that belong to a feature."""
    cmds = set()
    xpath = f".//feature[@name='{feature_name}']//require/command"
    for cmd_elem in xml_root.findall(xpath):
        name = cmd_elem.get("name", "")
        if name:
            cmds.add(name)
    return cmds


def extract_extension_commands(xml_root, extension_name: str) -> set[str]:
    """Extract commands that belong to an extension."""
    cmds = set()
    xpath = f".//extensions/extension[@name='{extension_name}']//require/command"
    for cmd_elem in xml_root.findall(xpath):
        name = cmd_elem.get("name", "")
        if name:
            cmds.add(name)
    return cmds


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xml", type=Path,
                        default=Path("third_party/Vulkan-Headers/registry/vk.xml"))
    parser.add_argument("--output", type=Path,
                        default=Path("gen/vulkan_api.json"))
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    xml_path = args.xml
    if not xml_path.exists():
        # Try common paths
        for alt in [
            Path("third_party/Vulkan-Headers/registry/vk.xml"),
            Path("C:/Users/kien/Documents/repos/OmniGPU/third_party/Vulkan-Headers/registry/vk.xml"),
        ]:
            if alt.exists():
                xml_path = alt
                break
        else:
            parser.error(f"vk.xml not found at {args.xml}")

    tree = ET.parse(str(xml_path))
    root = tree.getroot()
    load_type_info(root)

    # Collect core commands from all version-specific feature groups
    all_cmds = set()
    version_stats = {}
    for feat_name in CORE_VERSION_FEATURES:
        cmds = extract_feature_commands(root, feat_name)
        all_cmds.update(cmds)
        version_stats.setdefault(CORE_VERSION_FEATURES[feat_name], 0)
        version_stats[CORE_VERSION_FEATURES[feat_name]] += len(cmds)

    # Core extensions
    extension_list = [
        "VK_KHR_surface", "VK_KHR_swapchain",
        "VK_KHR_display",
        "VK_EXT_vertex_input_dynamic_state",
        "VK_EXT_private_data",
        "VK_EXT_tooling_info",
    ]
    for ext_name in extension_list:
        cmds = extract_extension_commands(root, ext_name)
        all_cmds.update(cmds)

    # Remove manually implemented functions
    all_cmds -= MANUAL_FUNCTIONS

    # Extract details
    details = extract_command_details(root, all_cmds)

    # Build output
    functions = []
    removed_no_params = 0
    for func_name in sorted(details.keys()):
        info = details[func_name]
        raw_params = info.get("params", [])
        if not raw_params:
            removed_no_params += 1
            continue

        classified = classify_params(func_name, raw_params)
        functions.append({
            "name": func_name,
            "return_type": info["return_type"],
            "params": classified,
        })

    # Print stats
    print("Core Vulkan commands by version:")
    for ver in sorted(version_stats.keys()):
        print(f"  Vulkan {ver}: {version_stats[ver]} commands")
    print(f"Extension commands: {sum(1 for c in all_cmds if not any(c.startswith(f'VK_VERSION_{v}') for v in ['1_0','1_1','1_2','1_3']))}")
    print(f"\nTotal after manual exclusion: {len(all_cmds)}")
    print(f"Functions in output: {len(functions)}")
    print(f"Skipped (no params / aliases): {removed_no_params}")

    if args.list:
        for f in functions:
            params = ", ".join(f"{p['type']} {p['name']}[{p['kind']}]" for p in f["params"])
            print(f"  {f['return_type']} {f['name']}({params})")
        return

    # Write
    output = {"functions": functions}
    output_path = args.output
    with open(output_path, "w", encoding="utf-8") as fp:
        json.dump(output, fp, indent=2, ensure_ascii=False)
    print(f"\nWritten to {output_path}")


if __name__ == "__main__":
    main()
