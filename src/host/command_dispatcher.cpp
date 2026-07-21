#include "command_dispatcher.h"
#include "vulkan_struct_deserializer.h"
#include "omnigpu_protocol_generated.h"
#include "../guest/vulkan_serializer.h"
#include <atomic>
#include <spdlog/spdlog.h>
#include <cstring>

namespace omnigpu::host {

// ---------------------------------------------------------------------------
// CommandDispatcher
// ---------------------------------------------------------------------------

CommandDispatcher::CommandDispatcher() {
    // Handlers are registered in the constructor
    #define REGISTER(id, fn) handlers_[static_cast<int>(id)] = fn

    // --- Queue ---
    REGISTER(fbs::FunctionId_vkQueueSubmit, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        VkQueue q = d.mapper_.queue();
        r.read_handle(); // queue handle (ignored, use ours)
        uint32_t count = r.read_u32();

        std::vector<VkSubmitInfo> submits(count);
        for (uint32_t i = 0; i < count; i++) {
            if (!read_VkSubmitInfo(r, &submits[i])) break;
            auto& si = submits[i];
            for (uint32_t j = 0; j < si.commandBufferCount; j++)
                const_cast<VkCommandBuffer*>(si.pCommandBuffers)[j] =
                    d.mapper_.get_command_buffer(handle_to_u64(si.pCommandBuffers[j]));
            for (uint32_t j = 0; j < si.waitSemaphoreCount; j++)
                const_cast<VkSemaphore*>(si.pWaitSemaphores)[j] =
                    d.mapper_.get_semaphore(handle_to_u64(si.pWaitSemaphores[j]));
            for (uint32_t j = 0; j < si.signalSemaphoreCount; j++)
                const_cast<VkSemaphore*>(si.pSignalSemaphores)[j] =
                    d.mapper_.get_semaphore(handle_to_u64(si.pSignalSemaphores[j]));
        }

        uint64_t guestFence = r.read_handle();
        VkFence fence = d.mapper_.get_fence(guestFence);
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        bool ownFence = false;
        if (fence == VK_NULL_HANDLE) {
            if (vkCreateFence(dev, &fci, nullptr, &fence) == VK_SUCCESS)
                ownFence = true;
        }

        VkResult res = vkQueueSubmit(q, count, submits.data(), fence);
        if (res == VK_SUCCESS) {
            d.pendingSubmitFence_ = fence;
            d.hasPendingSubmit_ = true;
            SPDLOG_DEBUG("  vkQueueSubmit ({} submits) -> submitted, fence={}",
                         count, (void*)fence);
        } else {
            SPDLOG_ERROR("  vkQueueSubmit failed: {}", static_cast<int>(res));
            if (ownFence) vkDestroyFence(dev, fence, nullptr);
        }

        for (uint32_t i = 0; i < count; i++)
            free_VkSubmitInfo(&submits[i]);
    });
    REGISTER(fbs::FunctionId_vkQueuePresentKHR, [](auto& d, auto& r) {
        r.read_handle(); // queue
        r.skip(sizeof(VkPresentInfoKHR));
        SPDLOG_DEBUG("  vkQueuePresentKHR -> handled at flush time");
    });
    REGISTER(fbs::FunctionId_vkDeviceWaitIdle, [](auto& d, auto& r) {
        r.read_handle(); // device
        vkDeviceWaitIdle(d.mapper_.device());
    });
    REGISTER(fbs::FunctionId_vkQueueWaitIdle, [](auto& d, auto& r) {
        r.read_handle(); // queue
        vkQueueWaitIdle(d.mapper_.queue());
    });

    // --- Command Pool ---
    REGISTER(fbs::FunctionId_vkCreateCommandPool, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkCommandPoolCreateInfo ci{};
        read_VkCommandPoolCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPool = r.read_handle();
        VkCommandPool pool;
        if (vkCreateCommandPool(d.mapper_.device(), &ci, nullptr, &pool) == VK_SUCCESS) {
            d.mapper_.store_command_pool(pPool, pool);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyCommandPool, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_command_pool(pool);
        if (p) vkDestroyCommandPool(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_command_pool(pool);
    });
    REGISTER(fbs::FunctionId_vkResetCommandPool, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        auto flags = r.read_u32();
        auto p = d.mapper_.get_command_pool(pool);
        if (p) vkResetCommandPool(d.mapper_.device(), p, static_cast<VkCommandPoolResetFlags>(flags));
    });
    REGISTER(fbs::FunctionId_vkAllocateCommandBuffers, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkCommandBufferAllocateInfo ai{};
        r.read_raw(&ai, sizeof(ai));
        ai.pNext = nullptr;
        ai.commandPool = d.mapper_.get_command_pool(handle_to_u64(ai.commandPool));
        uint32_t count = r.read_u32();
        std::vector<VkCommandBuffer> cbs(count);
        if (count > 0 && vkAllocateCommandBuffers(d.mapper_.device(), &ai, cbs.data()) == VK_SUCCESS) {
            for (uint32_t i = 0; i < count; i++) {
                uint64_t guestHandle = r.read_handle();
                d.mapper_.store_command_buffer(guestHandle, cbs[i]);
            }
        } else {
            for (uint32_t i = 0; i < count; i++) r.read_handle();
        }
    });
    REGISTER(fbs::FunctionId_vkFreeCommandBuffers, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        uint32_t count = r.read_u32();
        auto poolH = d.mapper_.get_command_pool(pool);
        std::vector<VkCommandBuffer> cbs;
        cbs.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gcb = r.read_handle();
            auto cb = d.mapper_.get_command_buffer(gcb);
            if (cb) cbs.push_back(cb);
            d.mapper_.remove_command_buffer(gcb);
        }
        if (!cbs.empty() && poolH)
            vkFreeCommandBuffers(d.mapper_.device(), poolH, static_cast<uint32_t>(cbs.size()), cbs.data());
    });
    REGISTER(fbs::FunctionId_vkBeginCommandBuffer, [](auto& d, auto& r) {
        uint64_t cb = r.read_handle();
        VkCommandBufferBeginInfo bi{};
        bi.sType = static_cast<VkStructureType>(r.read_u32());
        bi.pNext = nullptr;
        bi.flags = static_cast<VkCommandBufferUsageFlags>(r.read_u32());
        
        bool has_inherit = r.read_bool();
        VkCommandBufferInheritanceInfo inheritInfo{};
        if (has_inherit) {
            inheritInfo.sType = static_cast<VkStructureType>(r.read_u32());
            inheritInfo.pNext = nullptr;
            inheritInfo.renderPass = d.mapper_.get_render_pass(r.read_handle());
            inheritInfo.subpass = r.read_u32();
            inheritInfo.framebuffer = d.mapper_.get_framebuffer(r.read_handle());
            inheritInfo.occlusionQueryEnable = r.read_bool();
            inheritInfo.queryFlags = static_cast<VkQueryControlFlags>(r.read_u32());
            inheritInfo.pipelineStatistics = static_cast<VkQueryPipelineStatisticFlags>(r.read_u32());
            bi.pInheritanceInfo = &inheritInfo;
        } else {
            bi.pInheritanceInfo = nullptr;
        }

        d.mapper_.set_active_cmd(d.mapper_.get_command_buffer(cb));
        auto cmd = d.mapper_.active_cmd();
        if (cmd) vkBeginCommandBuffer(cmd, &bi);
    });
    REGISTER(fbs::FunctionId_vkEndCommandBuffer, [](auto& d, auto& r) {
        r.read_handle(); // command buffer
        auto cmd = d.mapper_.active_cmd();
        if (cmd) vkEndCommandBuffer(cmd);
    });
    REGISTER(fbs::FunctionId_vkResetCommandBuffer, [](auto& d, auto& r) {
        uint64_t cb = r.read_handle();
        auto flags = r.read_u32();
        auto cmd = d.mapper_.get_command_buffer(cb);
        if (cmd) vkResetCommandBuffer(cmd, static_cast<VkCommandBufferResetFlags>(flags));
    });

    // --- Memory ---
    REGISTER(fbs::FunctionId_vkAllocateMemory, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkMemoryAllocateInfo ai{};
        read_VkMemoryAllocateInfo(r, &ai);
        // Read pAllocator: bool + optional struct bytes
        if (r.read_bool()) {
            r.skip(sizeof(VkAllocationCallbacks));
        }
        uint64_t pMem = r.read_handle();

        // VRAM budget enforcement
        if (d.vramBudget_ > 0 && d.vramUsed_ + ai.allocationSize > d.vramBudget_) {
            SPDLOG_ERROR("vkAllocateMemory: VRAM budget exceeded (used={}MB, request={}MB, budget={}MB)",
                         d.vramUsed_ / (1024*1024), ai.allocationSize / (1024*1024),
                         d.vramBudget_ / (1024*1024));
            return;
        }

        VkDeviceMemory mem;
        VkResult res = vkAllocateMemory(d.mapper_.device(), &ai, nullptr, &mem);
        if (res == VK_SUCCESS) {
            d.mapper_.store_device_memory(pMem, mem);
            d.vramUsed_ += ai.allocationSize;
            d.memorySizes_[pMem] = ai.allocationSize;
        } else {
            SPDLOG_ERROR("vkAllocateMemory host failed: size={} type={} res={}",
                         ai.allocationSize, ai.memoryTypeIndex, static_cast<int>(res));
        }
    });
    REGISTER(fbs::FunctionId_vkFreeMemory, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t gMem = r.read_handle();
        // Read pAllocator: bool + optional struct bytes
        if (r.read_bool()) {
            r.skip(sizeof(VkAllocationCallbacks));
        }
        VkDeviceMemory hostMem = d.mapper_.get_device_memory(gMem);
        if (hostMem != VK_NULL_HANDLE) {
            vkFreeMemory(dev, hostMem, nullptr);
            d.mapper_.remove_device_memory(gMem);
            auto it = d.memorySizes_.find(gMem);
            if (it != d.memorySizes_.end()) {
                uint64_t size = it->second;
                d.vramUsed_ = d.vramUsed_ >= size ? d.vramUsed_ - size : 0;
                d.memorySizes_.erase(it);
            }
        } else {
            SPDLOG_WARN("vkFreeMemory: handle {:#x} not found (double-free?)", gMem);
        }
    });
    REGISTER(fbs::FunctionId_vkMapMemory, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_u64(); r.read_u64(); r.read_u32();
        r.read_handle(); // ppData (output pointer)
    });
    REGISTER(fbs::FunctionId_vkUnmapMemory, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkFlushMappedMemoryRanges, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gMem = r.read_handle();
            uint64_t offset = r.read_u64();
            uint64_t size = r.read_u64();

            std::vector<uint8_t> data(size);
            if (size > 0) {
                r.read_raw(data.data(), size);
            }

            VkDeviceMemory hostMem = d.mapper_.get_device_memory(gMem);
            if (hostMem != VK_NULL_HANDLE && size > 0) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(d.physDev_, &props);
                uint64_t alignment = props.limits.minMemoryMapAlignment;
                if (alignment == 0) alignment = 64;

                uint64_t aligned_offset = (offset / alignment) * alignment;
                uint64_t alignment_diff = offset - aligned_offset;
                uint64_t aligned_size = size + alignment_diff;

                void* mapped = nullptr;
                VkResult res = vkMapMemory(dev, hostMem, aligned_offset, aligned_size, 0, &mapped);
                if (res == VK_SUCCESS && mapped) {
                    uint8_t* target = static_cast<uint8_t*>(mapped) + alignment_diff;
                    std::memcpy(target, data.data(), static_cast<size_t>(size));
                    vkUnmapMemory(dev, hostMem);
                } else {
                    SPDLOG_ERROR("vkFlushMappedMemoryRanges: map failed (res={})", static_cast<int>(res));
                }
            }
        }
    });
    REGISTER(fbs::FunctionId_vkInvalidateMappedMemoryRanges, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gMem = r.read_handle();
            uint64_t offset = r.read_u64();
            uint64_t size = r.read_u64();

            VkDeviceMemory hostMem = d.mapper_.get_device_memory(gMem);
            if (hostMem != VK_NULL_HANDLE && size > 0) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(d.physDev_, &props);
                uint64_t alignment = props.limits.minMemoryMapAlignment;
                if (alignment == 0) alignment = 64;

                uint64_t aligned_offset = (offset / alignment) * alignment;
                uint64_t alignment_diff = offset - aligned_offset;
                uint64_t aligned_size = size + alignment_diff;

                void* mapped = nullptr;
                VkResult res = vkMapMemory(dev, hostMem, aligned_offset, aligned_size, 0, &mapped);
                if (res == VK_SUCCESS && mapped) {
                    const uint8_t* source = static_cast<const uint8_t*>(mapped) + alignment_diff;
                    if (d.sendDataFn_) {
                        d.sendDataFn_(gMem, source, static_cast<size_t>(size), offset);
                    }
                    vkUnmapMemory(dev, hostMem);
                }
            }
        }
    });
    REGISTER(fbs::FunctionId_vkBindBufferMemory, [](auto& d, auto& r) {
        r.read_handle();
        auto buf = r.read_handle(); auto mem = r.read_handle(); auto off = r.read_u64();
        auto b = d.mapper_.get_buffer(buf);
        auto m = d.mapper_.get_device_memory(mem);
        if (b && m) vkBindBufferMemory(d.mapper_.device(), b, m, off);
    });
    REGISTER(fbs::FunctionId_vkBindImageMemory, [](auto& d, auto& r) {
        r.read_handle();
        auto img = r.read_handle(); auto mem = r.read_handle(); auto off = r.read_u64();
        auto i = d.mapper_.get_image(img);
        auto m = d.mapper_.get_device_memory(mem);
        if (i && m) vkBindImageMemory(d.mapper_.device(), i, m, off);
    });

    // --- Buffer ---
    REGISTER(fbs::FunctionId_vkCreateBuffer, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkBufferCreateInfo ci{};
        read_VkBufferCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pBuf = r.read_handle();
        VkBuffer buf;
        if (vkCreateBuffer(d.mapper_.device(), &ci, nullptr, &buf) == VK_SUCCESS) {
            d.mapper_.store_buffer(pBuf, buf);
        }
        delete[] ci.pQueueFamilyIndices;
    });
    REGISTER(fbs::FunctionId_vkDestroyBuffer, [](auto& d, auto& r) {
        r.read_handle(); auto buf = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto b = d.mapper_.get_buffer(buf);
        if (b) vkDestroyBuffer(d.mapper_.device(), b, nullptr);
        d.mapper_.remove_buffer(buf);
    });
    REGISTER(fbs::FunctionId_vkCreateBufferView, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkBufferViewCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        ci.buffer = d.mapper_.get_buffer(handle_to_u64(ci.buffer));
        if (r.read_bool()) r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pView = r.read_handle();
        VkBufferView view;
        if (ci.buffer && vkCreateBufferView(d.mapper_.device(), &ci, nullptr, &view) == VK_SUCCESS) {
            d.mapper_.store_buffer_view(pView, view);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyBufferView, [](auto& d, auto& r) {
        r.read_handle(); uint64_t gView = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto view = d.mapper_.get_buffer_view(gView);
        if (view) vkDestroyBufferView(d.mapper_.device(), view, nullptr);
        d.mapper_.remove_buffer_view(gView);
    });

    // --- Image ---
    REGISTER(fbs::FunctionId_vkCreateImage, [](auto& d, auto& r) {
        r.read_handle();
        VkImageCreateInfo ci{};
        read_VkImageCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pImg = r.read_handle();
        VkImage img;
        // Skip images with DEPTH_STENCIL usage — format numbers differ between
        // guest Vulkan headers (1.3) and host (1.4), causing the host to
        // misinterpret the format and crash the NVIDIA driver (divide-by-zero).
        if (ci.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            SPDLOG_WARN("vkCreateImage: skipping DEPTH_STENCIL image (fmt={} usage={:#x}) - Vulkan version mismatch workaround",
                        static_cast<int>(ci.format), ci.usage);
        } else if (vkCreateImage(d.mapper_.device(), &ci, nullptr, &img) == VK_SUCCESS) {
            d.mapper_.store_image(pImg, img);
        }
        delete[] ci.pQueueFamilyIndices;
    });
    REGISTER(fbs::FunctionId_vkDestroyImage, [](auto& d, auto& r) {
        r.read_handle(); auto img = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto i = d.mapper_.get_image(img);
        if (i) vkDestroyImage(d.mapper_.device(), i, nullptr);
        d.mapper_.remove_image(img);
    });
    REGISTER(fbs::FunctionId_vkCreateImageView, [](auto& d, auto& r) {
        r.read_handle();
        VkImageViewCreateInfo ci{};
        read_VkImageViewCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pView = r.read_handle();
        uint64_t gImg = handle_to_u64(ci.image);
        VkImage hostImg = d.mapper_.get_image(gImg);
        if (hostImg == VK_NULL_HANDLE) {
            if (d.colorImage_ != VK_NULL_HANDLE) {
                // Unregistered image -> assume it is a guest swapchain image.
                // Map it to our offscreen colorImage_.
                hostImg = d.colorImage_;
                ci.format = VK_FORMAT_R8G8B8A8_UNORM;
            } else {
                SPDLOG_WARN("vkCreateImageView: cannot resolve image handle {:#x}, no fallback available", gImg);
            }
        }
        ci.image = hostImg;
        VkImageView view;
        if (vkCreateImageView(d.mapper_.device(), &ci, nullptr, &view) == VK_SUCCESS) {
            d.mapper_.store_image_view(pView, view);
            d.mapper_.store_view_image(pView, ci.image);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyImageView, [](auto& d, auto& r) {
        r.read_handle(); auto v = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto view = d.mapper_.get_image_view(v);
        if (view) vkDestroyImageView(d.mapper_.device(), view, nullptr);
        d.mapper_.remove_image_view(v);
    });

    // --- Sampler ---
    REGISTER(fbs::FunctionId_vkCreateSampler, [](auto& d, auto& r) {
        r.read_handle();
        VkSamplerCreateInfo ci{};
        read_VkSamplerCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSamp = r.read_handle();
        VkSampler samp;
        if (vkCreateSampler(d.mapper_.device(), &ci, nullptr, &samp) == VK_SUCCESS) {
            d.mapper_.store_sampler(pSamp, samp);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroySampler, [](auto& d, auto& r) {
        r.read_handle(); auto s = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto samp = d.mapper_.get_sampler(s);
        if (samp) vkDestroySampler(d.mapper_.device(), samp, nullptr);
        d.mapper_.remove_sampler(s);
    });

    // --- Shader Module ---
    REGISTER(fbs::FunctionId_vkCreateShaderModule, [](auto& d, auto& r) {
        r.read_handle();
        // Custom serialization: flags + codeSize + pCode data
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.flags = r.read_u32();
        ci.codeSize = static_cast<size_t>(r.read_u64());
        std::vector<uint32_t> spirv;
        if (ci.codeSize > 0) {
            spirv.resize((ci.codeSize + 3) / 4, 0);
            r.read_raw(spirv.data(), static_cast<size_t>(ci.codeSize));
            ci.pCode = spirv.data();
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSM = r.read_handle();
        VkShaderModule sm;
        if (vkCreateShaderModule(d.mapper_.device(), &ci, nullptr, &sm) == VK_SUCCESS) {
            d.mapper_.store_shader_module(pSM, sm);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyShaderModule, [](auto& d, auto& r) {
        r.read_handle(); auto s = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto sm = d.mapper_.get_shader_module(s);
        if (sm) vkDestroyShaderModule(d.mapper_.device(), sm, nullptr);
        d.mapper_.remove_shader_module(s);
    });

    // --- Pipeline Layout ---
    REGISTER(fbs::FunctionId_vkCreatePipelineLayout, [](auto& d, auto& r) {
        r.read_handle();
        VkPipelineLayoutCreateInfo ci{};
        read_VkPipelineLayoutCreateInfo(r, &ci);
        // Remap descriptor set layout handles (guest → host)
        for (uint32_t i = 0; i < ci.setLayoutCount; i++)
            const_cast<VkDescriptorSetLayout*>(ci.pSetLayouts)[i] =
                d.mapper_.get_dsl(handle_to_u64(ci.pSetLayouts[i]));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPL = r.read_handle();
        VkPipelineLayout pl;
        if (vkCreatePipelineLayout(d.mapper_.device(), &ci, nullptr, &pl) == VK_SUCCESS) {
            d.mapper_.store_pipeline_layout(pPL, pl);
        }
        free_VkPipelineLayoutCreateInfo(&ci);
    });
    REGISTER(fbs::FunctionId_vkDestroyPipelineLayout, [](auto& d, auto& r) {
        r.read_handle(); auto pl = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline_layout(pl);
        if (p) vkDestroyPipelineLayout(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_pipeline_layout(pl);
    });

    // --- Pipelines (complex struct with pointer chains) ---
    REGISTER(fbs::FunctionId_vkCreateGraphicsPipelines, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t pCache = r.read_handle();
        VkPipelineCache cache = d.mapper_.get_pipeline_cache(pCache);
        uint32_t count = r.read_u32();

        SPDLOG_INFO("vkCreateGraphicsPipelines: count={}, cache={}", count, (void*)cache);
        std::vector<VkGraphicsPipelineCreateInfo> infos(count);
        for (uint32_t i = 0; i < count; i++) {
            SPDLOG_INFO("vkCreateGraphicsPipelines: deserializing info {}", i);
            read_VkGraphicsPipelineCreateInfo(r, &infos[i]);
        }

        // Remap guest handles to host handles
        for (uint32_t i = 0; i < count; i++) {
            if (infos[i].pStages) {
                for (uint32_t j = 0; j < infos[i].stageCount; j++) {
                    auto* stage = const_cast<VkPipelineShaderStageCreateInfo*>(&infos[i].pStages[j]);
                    auto guestModule = (uint64_t)stage->module;
                    stage->module = d.mapper_.get_shader_module(guestModule);
                    SPDLOG_INFO("  Remapped stage {} shader module guest={} -> host={}", j, guestModule, (void*)stage->module);
                }
            }
            auto guestLayout = (uint64_t)infos[i].layout;
            infos[i].layout = d.mapper_.get_pipeline_layout(guestLayout);
            SPDLOG_INFO("  Remapped pipeline layout guest={} -> host={}", guestLayout, (void*)infos[i].layout);

            auto guestRenderPass = (uint64_t)infos[i].renderPass;
            infos[i].renderPass = d.mapper_.get_render_pass(guestRenderPass);
            SPDLOG_INFO("  Remapped render pass guest={} -> host={}", guestRenderPass, (void*)infos[i].renderPass);

            auto guestBasePipeline = (uint64_t)infos[i].basePipelineHandle;
            if (guestBasePipeline != 0) {
                infos[i].basePipelineHandle = d.mapper_.get_pipeline(guestBasePipeline);
                SPDLOG_INFO("  Remapped base pipeline guest={} -> host={}", guestBasePipeline, (void*)infos[i].basePipelineHandle);
            }
        }

        r.skip(sizeof(VkAllocationCallbacks));

        std::vector<VkPipeline> pipelines(count);
        SPDLOG_INFO("vkCreateGraphicsPipelines: calling Vulkan driver on device={}", (void*)dev);
        VkResult res = vkCreateGraphicsPipelines(dev, cache, count,
                                                  infos.data(), nullptr,
                                                  pipelines.data());
        SPDLOG_INFO("vkCreateGraphicsPipelines: Vulkan driver call returned res={}", (int)res);
        uint32_t guestCount = r.read_u32();
        for (uint32_t i = 0; i < guestCount; i++) {
            uint64_t guestPipeline = r.read_handle();
            if (res == VK_SUCCESS && i < count) {
                d.mapper_.store_pipeline(guestPipeline, pipelines[i]);
            }
        }
        for (auto& info : infos) {
            SPDLOG_INFO("vkCreateGraphicsPipelines: freeing info memory");
            free_VkGraphicsPipelineCreateInfo(&info);
        }
        SPDLOG_INFO("vkCreateGraphicsPipelines: completed successfully");
    });
    REGISTER(fbs::FunctionId_vkCreateComputePipelines, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); uint64_t pCache = r.read_handle();
        VkPipelineCache cache = d.mapper_.get_pipeline_cache(pCache);
        uint32_t count = r.read_u32();

        std::vector<VkComputePipelineCreateInfo> infos(count);
        for (uint32_t i = 0; i < count; i++)
            read_VkComputePipelineCreateInfo(r, &infos[i]);

        // Remap guest handles to host handles
        for (uint32_t i = 0; i < count; i++) {
            auto guestModule = (uint64_t)infos[i].stage.module;
            infos[i].stage.module = d.mapper_.get_shader_module(guestModule);

            auto guestLayout = (uint64_t)infos[i].layout;
            infos[i].layout = d.mapper_.get_pipeline_layout(guestLayout);

            auto guestBasePipeline = (uint64_t)infos[i].basePipelineHandle;
            if (guestBasePipeline != 0) {
                infos[i].basePipelineHandle = d.mapper_.get_pipeline(guestBasePipeline);
            }
        }

        r.skip(sizeof(VkAllocationCallbacks));

        std::vector<VkPipeline> pipelines(count);
        VkResult res = vkCreateComputePipelines(dev, cache, count,
                                                 infos.data(), nullptr,
                                                 pipelines.data());
        uint32_t guestCount2 = r.read_u32();
        for (uint32_t i = 0; i < guestCount2; i++) {
            uint64_t guestPipeline = r.read_handle();
            if (res == VK_SUCCESS && i < count) {
                d.mapper_.store_pipeline(guestPipeline, pipelines[i]);
            }
        }
        for (auto& info : infos)
            free_VkComputePipelineCreateInfo(&info);
        SPDLOG_DEBUG("  vkCreateComputePipelines ({} pipelines) result={}", count, static_cast<int>(res));
    });
    REGISTER(fbs::FunctionId_vkDestroyPipeline, [](auto& d, auto& r) {
        r.read_handle(); auto pp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline(pp);
        if (p) vkDestroyPipeline(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_pipeline(pp);
    });
    REGISTER(fbs::FunctionId_vkCreatePipelineCache, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkPipelineCacheCreateInfo));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPC = r.read_handle();
        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VkPipelineCache pc;
        if (vkCreatePipelineCache(d.mapper_.device(), &ci, nullptr, &pc) == VK_SUCCESS) {
            d.mapper_.store_pipeline_cache(pPC, pc);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPipelineCache, [](auto& d, auto& r) {
        r.read_handle(); auto pc = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline_cache(pc);
        if (p) vkDestroyPipelineCache(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_pipeline_cache(pc);
    });

    // --- Render Pass ---
    REGISTER(fbs::FunctionId_vkCreateRenderPass, [](auto& d, auto& r) {
        r.read_handle();
        VkRenderPassCreateInfo ci{};
        read_VkRenderPassCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pRP = r.read_handle();
        VkRenderPass rp;
        VkResult res = vkCreateRenderPass(d.mapper_.device(), &ci, nullptr, &rp);
        if (res == VK_SUCCESS) {
            d.mapper_.store_render_pass(pRP, rp);
        }
        free_VkRenderPassCreateInfo(&ci);
    });
    REGISTER(fbs::FunctionId_vkDestroyRenderPass, [](auto& d, auto& r) {
        r.read_handle(); auto rp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto rph = d.mapper_.get_render_pass(rp);
        if (rph) vkDestroyRenderPass(d.mapper_.device(), rph, nullptr);
        d.mapper_.remove_render_pass(rp);
    });
    REGISTER(fbs::FunctionId_vkCreateRenderPass2, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkRenderPassCreateInfo2));
        r.skip(sizeof(VkAllocationCallbacks)); r.read_handle();
    });

    // --- Framebuffer ---
    REGISTER(fbs::FunctionId_vkCreateFramebuffer, [](auto& d, auto& r) {
        r.read_handle();
        VkFramebufferCreateInfo ci{};
        read_VkFramebufferCreateInfo(r, &ci);
        VkImage firstImg = VK_NULL_HANDLE;
        for (uint32_t i = 0; i < ci.attachmentCount; i++) {
            uint64_t gView = handle_to_u64(ci.pAttachments[i]);
            VkImageView hostView = d.mapper_.get_image_view(gView);
            if (i == 0) {
                firstImg = d.mapper_.get_view_image(gView);
            }
            const_cast<VkImageView*>(ci.pAttachments)[i] = hostView;
        }
        if (firstImg == VK_NULL_HANDLE)
            firstImg = d.colorImage_;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pFB = r.read_handle();
        VkFramebuffer fb;
        if (vkCreateFramebuffer(d.mapper_.device(), &ci, nullptr, &fb) == VK_SUCCESS) {
            d.mapper_.store_framebuffer(pFB, fb);
            d.framebufferRenderTarget_[pFB] = firstImg;
        }
        free_VkFramebufferCreateInfo(&ci);
    });
    REGISTER(fbs::FunctionId_vkDestroyFramebuffer, [](auto& d, auto& r) {
        r.read_handle(); auto fb = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto f = d.mapper_.get_framebuffer(fb);
        if (f) vkDestroyFramebuffer(d.mapper_.device(), f, nullptr);
        d.mapper_.remove_framebuffer(fb);
        d.framebufferRenderTarget_.erase(fb);
    });

    // --- Descriptor Set Layout ---
    REGISTER(fbs::FunctionId_vkCreateDescriptorSetLayout, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorSetLayoutCreateInfo ci{};
        read_VkDescriptorSetLayoutCreateInfo(r, &ci);
        // Remap immutable sampler handles
        for (uint32_t b = 0; b < ci.bindingCount; b++) {
            auto* bind = const_cast<VkDescriptorSetLayoutBinding*>(&ci.pBindings[b]);
            if (bind->pImmutableSamplers) {
                for (uint32_t s = 0; s < bind->descriptorCount; s++)
                    const_cast<VkSampler*>(bind->pImmutableSamplers)[s] =
                        d.mapper_.get_sampler(handle_to_u64(bind->pImmutableSamplers[s]));
            }
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pDSL = r.read_handle();
        VkDescriptorSetLayout dsl;
        if (vkCreateDescriptorSetLayout(d.mapper_.device(), &ci, nullptr, &dsl) == VK_SUCCESS) {
            d.mapper_.store_descriptor_set_layout(pDSL, dsl);
        }
        free_VkDescriptorSetLayoutCreateInfo(&ci);
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorSetLayout, [](auto& d, auto& r) {
        r.read_handle(); auto g = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto h = d.mapper_.get_dsl(g);
        if (h) vkDestroyDescriptorSetLayout(d.mapper_.device(), h, nullptr);
        d.mapper_.remove_dsl(g);
    });
    REGISTER(fbs::FunctionId_vkCreateDescriptorPool, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorPoolCreateInfo ci{};
        read_VkDescriptorPoolCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pDP = r.read_handle();
        VkDescriptorPool dp;
        if (vkCreateDescriptorPool(d.mapper_.device(), &ci, nullptr, &dp) == VK_SUCCESS) {
            d.mapper_.store_descriptor_pool(pDP, dp);
        }
        delete[] ci.pPoolSizes;
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorPool, [](auto& d, auto& r) {
        r.read_handle(); auto dp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_dp(dp);
        if (p) vkDestroyDescriptorPool(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_dp(dp);
    });
    REGISTER(fbs::FunctionId_vkAllocateDescriptorSets, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorSetAllocateInfo ai{};
        read_VkDescriptorSetAllocateInfo(r, &ai);
        // Remap pool and set layout handles (guest → host)
        ai.descriptorPool = d.mapper_.get_dp(handle_to_u64(ai.descriptorPool));
        for (uint32_t i = 0; i < ai.descriptorSetCount; i++)
            const_cast<VkDescriptorSetLayout*>(ai.pSetLayouts)[i] =
                d.mapper_.get_dsl(handle_to_u64(ai.pSetLayouts[i]));

        std::vector<VkDescriptorSet> ds(ai.descriptorSetCount);
        VkResult res = vkAllocateDescriptorSets(d.mapper_.device(), &ai, ds.data());

        uint32_t guestCount = r.read_u32();
        for (uint32_t i = 0; i < guestCount; i++) {
            uint64_t guestDS = r.read_handle();
            if (res == VK_SUCCESS && i < ai.descriptorSetCount) {
                d.mapper_.store_descriptor_set(guestDS, ds[i]);
            }
        }
        delete[] ai.pSetLayouts;
    });
    REGISTER(fbs::FunctionId_vkFreeDescriptorSets, [](auto& d, auto& r) {
        r.read_handle();
        auto dp = r.read_handle();
        uint32_t count = r.read_u32();
        auto v = r.template read_array<uint64_t>(count);
        auto pool = d.mapper_.get_dp(dp);
        for (auto& gds : v) {
            auto ds = d.mapper_.get_ds(gds);
            if (ds && pool) vkFreeDescriptorSets(d.mapper_.device(), pool, 1, &ds);
            d.mapper_.remove_ds(gds);
        }
    });
    REGISTER(fbs::FunctionId_vkUpdateDescriptorSets, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t wc = r.read_u32();
        SPDLOG_INFO("vkUpdateDescriptorSets: wc={}", wc);
        std::vector<VkWriteDescriptorSet> writes(wc);
        std::vector<VkDescriptorImageInfo*> imgPtrs(wc, nullptr);
        std::vector<VkDescriptorBufferInfo*> bufPtrs(wc, nullptr);
        std::vector<VkBufferView*> viewPtrs(wc, nullptr);
        for (uint32_t i = 0; i < wc; i++) {
            SPDLOG_INFO("  reading write {}", i);
            read_VkWriteDescriptorSet(r, &writes[i], &imgPtrs[i], &bufPtrs[i], &viewPtrs[i]);
            SPDLOG_INFO("  write {} ok, dstSet={}, type={}, count={}", i,
                (uint64_t)writes[i].dstSet, (int)writes[i].descriptorType, writes[i].descriptorCount);
        }
        uint32_t cc = r.read_u32();
        SPDLOG_INFO("vkUpdateDescriptorSets: cc={}", cc);
        std::vector<VkCopyDescriptorSet> copies(cc);
        for (uint32_t i = 0; i < cc; i++) {
            r.read_raw(&copies[i], sizeof(VkCopyDescriptorSet));
            copies[i].pNext = nullptr;
            copies[i].srcSet = d.mapper_.get_ds(handle_to_u64(copies[i].srcSet));
            copies[i].dstSet = d.mapper_.get_ds(handle_to_u64(copies[i].dstSet));
        }

        // Remap handles
        SPDLOG_INFO("vkUpdateDescriptorSets: remapping handles");
        for (auto& w : writes) {
            w.dstSet = d.mapper_.get_ds(handle_to_u64(w.dstSet));
            if (w.pImageInfo) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* img = const_cast<VkDescriptorImageInfo*>(&w.pImageInfo[j]);
                    img->sampler = d.mapper_.get_sampler(handle_to_u64(img->sampler));
                    img->imageView = d.mapper_.get_image_view(handle_to_u64(img->imageView));
                }
            }
            if (w.pBufferInfo) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* buf = const_cast<VkDescriptorBufferInfo*>(&w.pBufferInfo[j]);
                    buf->buffer = d.mapper_.get_buffer(handle_to_u64(buf->buffer));
                }
            }
        }
        SPDLOG_INFO("vkUpdateDescriptorSets: calling driver");
        vkUpdateDescriptorSets(d.mapper_.device(), wc, writes.data(), cc, copies.data());
        SPDLOG_INFO("vkUpdateDescriptorSets: done ({} writes, {} copies)", wc, cc);
        for (uint32_t i = 0; i < wc; i++) {
            delete[] imgPtrs[i]; delete[] bufPtrs[i]; delete[] viewPtrs[i];
        }
    });
    REGISTER(fbs::FunctionId_vkResetDescriptorPool, [](auto& d, auto& r) {
        r.read_handle(); auto dp = r.read_handle(); r.read_u32();
        auto p = d.mapper_.get_dp(dp);
        if (p) vkResetDescriptorPool(d.mapper_.device(), p, 0);
    });

    // --- Fence ---
    REGISTER(fbs::FunctionId_vkCreateFence, [](auto& d, auto& r) {
        r.read_handle();
        VkFenceCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pFence = r.read_handle();
        VkFence fence;
        if (vkCreateFence(d.mapper_.device(), &ci, nullptr, &fence) == VK_SUCCESS) {
            d.mapper_.store_fence(pFence, fence);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyFence, [](auto& d, auto& r) {
        r.read_handle(); auto f = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto fence = d.mapper_.get_fence(f);
        if (fence) vkDestroyFence(d.mapper_.device(), fence, nullptr);
        d.mapper_.remove_fence(f);
    });
    REGISTER(fbs::FunctionId_vkWaitForFences, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkFence> fences(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gFence = r.read_handle();
            fences[i] = d.mapper_.get_fence(gFence);
        }
        VkBool32 waitAll = r.read_bool();
        uint64_t timeout = r.read_u64();
        VkResult res = vkWaitForFences(d.mapper_.device(), count, fences.data(), waitAll, timeout);
        if (res != VK_SUCCESS) {
            SPDLOG_WARN("vkWaitForFences: timeout or error (count={}, timeout={}ms, res={})",
                        count, timeout, static_cast<int>(res));
        }
    });
    REGISTER(fbs::FunctionId_vkResetFences, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkFence> fences(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gFence = r.read_handle();
            fences[i] = d.mapper_.get_fence(gFence);
        }
        if (!fences.empty())
            vkResetFences(d.mapper_.device(), count, fences.data());
    });

    // --- Semaphore ---
    REGISTER(fbs::FunctionId_vkCreateSemaphore, [](auto& d, auto& r) {
        r.read_handle();
        VkSemaphoreCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ci.flags = r.read_u32();
        // Check for timeline semaphore extension
        VkSemaphoreTypeCreateInfo ti{};
        ti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        bool has_timeline = r.read_bool() != VK_FALSE;
        if (has_timeline) {
            ti.semaphoreType = static_cast<VkSemaphoreType>(r.read_u32());
            ti.initialValue = r.read_u64();
            ci.pNext = &ti;
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSem = r.read_handle();
        VkSemaphore sem;
        if (vkCreateSemaphore(d.mapper_.device(), &ci, nullptr, &sem) == VK_SUCCESS) {
            d.mapper_.store_semaphore(pSem, sem);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroySemaphore, [](auto& d, auto& r) {
        r.read_handle(); auto s = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto sem = d.mapper_.get_semaphore(s);
        if (sem) vkDestroySemaphore(d.mapper_.device(), sem, nullptr);
        d.mapper_.remove_semaphore(s);
    });

    // --- Event ---
    REGISTER(fbs::FunctionId_vkCreateEvent, [](auto& d, auto& r) {
        r.read_handle();
        VkEventCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pEv = r.read_handle();
        VkEvent ev;
        if (vkCreateEvent(d.mapper_.device(), &ci, nullptr, &ev) == VK_SUCCESS) {
            d.mapper_.store_event(pEv, ev);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyEvent, [](auto& d, auto& r) {
        r.read_handle(); auto e = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto ev = d.mapper_.get_event(e);
        if (ev) vkDestroyEvent(d.mapper_.device(), ev, nullptr);
        d.mapper_.remove_event(e);
    });
    REGISTER(fbs::FunctionId_vkSetEvent, [](auto& d, auto& r) {
        r.read_handle(); auto e = r.read_handle();
        auto ev = d.mapper_.get_event(e);
        if (ev) vkSetEvent(d.mapper_.device(), ev);
    });
    REGISTER(fbs::FunctionId_vkResetEvent, [](auto& d, auto& r) {
        r.read_handle(); auto e = r.read_handle();
        auto ev = d.mapper_.get_event(e);
        if (ev) vkResetEvent(d.mapper_.device(), ev);
    });

    // --- Query Pool ---
    REGISTER(fbs::FunctionId_vkCreateQueryPool, [](auto& d, auto& r) {
        r.read_handle();
        VkQueryPoolCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pQP = r.read_handle();
        VkQueryPool qp;
        if (vkCreateQueryPool(d.mapper_.device(), &ci, nullptr, &qp) == VK_SUCCESS) {
            d.mapper_.store_query_pool(pQP, qp);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyQueryPool, [](auto& d, auto& r) {
        r.read_handle(); auto qp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto q = d.mapper_.get_query_pool(qp);
        if (q) vkDestroyQueryPool(d.mapper_.device(), q, nullptr);
        d.mapper_.remove_query_pool(qp);
    });

    // --- Command Recording ---
    REGISTER(fbs::FunctionId_vkCmdBindPipeline, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle(); // cb
        VkPipelineBindPoint bp = static_cast<VkPipelineBindPoint>(r.read_u32());
        uint64_t pp = r.read_handle();
        auto ppl = d.mapper_.get_pipeline(pp);
        if (cb && ppl) vkCmdBindPipeline(cb, bp, ppl);
    });
    REGISTER(fbs::FunctionId_vkCmdBindVertexBuffers, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32();
        uint32_t count = r.read_u32();
        auto buf_handles = r.template read_array<uint64_t>(count);
        auto offsets = r.template read_array<uint64_t>(count);
        if (cb && buf_handles.size() > 0) {
            std::vector<VkBuffer> bufs;
            for (auto& g : buf_handles) bufs.push_back(d.mapper_.get_buffer(g));
            vkCmdBindVertexBuffers(cb, first, static_cast<uint32_t>(bufs.size()),
                                   bufs.data(), offsets.data());
        }
    });
    REGISTER(fbs::FunctionId_vkCmdBindIndexBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t buf = r.read_handle();
        VkDeviceSize off = r.read_u64();
        VkIndexType idxType = static_cast<VkIndexType>(r.read_u32());
        auto b = d.mapper_.get_buffer(buf);
        if (cb && b) vkCmdBindIndexBuffer(cb, b, off, idxType);
    });
    REGISTER(fbs::FunctionId_vkCmdBindDescriptorSets, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineBindPoint bp = static_cast<VkPipelineBindPoint>(r.read_u32());
        uint64_t layout = r.read_handle();
        uint32_t firstSet = r.read_u32();
        uint32_t count = r.read_u32();
        auto desc_handles = r.template read_array<uint64_t>(count);
        uint32_t dynCount = r.read_u32();
        auto dynOffsets = r.template read_array<uint32_t>(dynCount);
        if (cb) {
            VkPipelineLayout pl = d.mapper_.get_pipeline_layout(layout);
            if (!pl) { SPDLOG_WARN("vkCmdBindDescriptorSets: null pipeline layout"); return; }
            std::vector<VkDescriptorSet> dss;
            for (auto& g : desc_handles) dss.push_back(d.mapper_.get_ds(g));
            vkCmdBindDescriptorSets(cb, bp, pl, firstSet,
                                    static_cast<uint32_t>(dss.size()),
                                    dss.data(), dynCount, dynOffsets.data());
        }
    });
    REGISTER(fbs::FunctionId_vkCmdSetViewport, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32();
        uint32_t count = r.read_u32();
        auto vps = r.template read_array<VkViewport>(count);
        if (cb) vkCmdSetViewport(cb, first, static_cast<uint32_t>(vps.size()), vps.data());
    });
    REGISTER(fbs::FunctionId_vkCmdSetScissor, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32();
        uint32_t count = r.read_u32();
        auto scis = r.template read_array<VkRect2D>(count);
        if (cb) vkCmdSetScissor(cb, first, static_cast<uint32_t>(scis.size()), scis.data());
    });
    REGISTER(fbs::FunctionId_vkCmdPushConstants, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t layout = r.read_handle();
        VkShaderStageFlags stages = r.read_u32();
        uint32_t offset = r.read_u32();
        uint32_t size = r.read_u32();
        auto data = r.template read_array<uint8_t>(size);
        VkPipelineLayout pl = d.mapper_.get_pipeline_layout(layout);
        if (cb && data.size() > 0)
            vkCmdPushConstants(cb, pl, stages, offset, static_cast<uint32_t>(data.size()), data.data());
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthBias, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        float cf = r.read_f32(); float clamp = r.read_f32(); float sf = r.read_f32();
        if (cb) vkCmdSetDepthBias(cb, cf, clamp, sf);
    });
    REGISTER(fbs::FunctionId_vkCmdSetLineWidth, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        float w = r.read_f32();
        if (cb) vkCmdSetLineWidth(cb, w);
    });
    REGISTER(fbs::FunctionId_vkCmdSetBlendConstants, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        float bc[4]; r.read_raw(bc, 16);
        if (cb) vkCmdSetBlendConstants(cb, bc);
    });
    REGISTER(fbs::FunctionId_vkCmdSetStencilCompareMask, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        auto fm = r.read_u32(); auto cm = r.read_u32();
        if (cb) vkCmdSetStencilCompareMask(cb, static_cast<VkStencilFaceFlags>(fm), cm);
    });
    REGISTER(fbs::FunctionId_vkCmdSetStencilWriteMask, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        auto fm = r.read_u32(); auto wm = r.read_u32();
        if (cb) vkCmdSetStencilWriteMask(cb, static_cast<VkStencilFaceFlags>(fm), wm);
    });
    REGISTER(fbs::FunctionId_vkCmdSetStencilReference, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        auto fm = r.read_u32(); auto ref = r.read_u32();
        if (cb) vkCmdSetStencilReference(cb, static_cast<VkStencilFaceFlags>(fm), ref);
    });

    // --- Draw / Dispatch ---
    REGISTER(fbs::FunctionId_vkCmdDraw, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t vc = r.read_u32(); uint32_t ic = r.read_u32();
        uint32_t fv = r.read_u32(); uint32_t fi = r.read_u32();
        if (cb) vkCmdDraw(cb, vc, ic, fv, fi);
    });
    REGISTER(fbs::FunctionId_vkCmdDrawIndexed, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ic = r.read_u32(); uint32_t inst = r.read_u32();
        uint32_t fi = r.read_u32(); int32_t vo = r.read_i32(); uint32_t fInst = r.read_u32();
        if (cb) vkCmdDrawIndexed(cb, ic, inst, fi, vo, fInst);
    });
    REGISTER(fbs::FunctionId_vkCmdDispatch, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t x = r.read_u32(); uint32_t y = r.read_u32(); uint32_t z = r.read_u32();
        if (cb) vkCmdDispatch(cb, x, y, z);
    });
    REGISTER(fbs::FunctionId_vkCmdDrawIndirect, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t buf = r.read_handle(); uint64_t off = r.read_u64();
        uint32_t dc = r.read_u32(); uint32_t stride = r.read_u32();
        auto b = d.mapper_.get_buffer(buf);
        if (cb && b) vkCmdDrawIndirect(cb, b, off, dc, stride);
    });
    REGISTER(fbs::FunctionId_vkCmdDrawIndexedIndirect, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t buf = r.read_handle(); uint64_t off = r.read_u64();
        uint32_t dc = r.read_u32(); uint32_t stride = r.read_u32();
        auto b = d.mapper_.get_buffer(buf);
        if (cb && b) vkCmdDrawIndexedIndirect(cb, b, off, dc, stride);
    });

    // --- Copy ---
    REGISTER(fbs::FunctionId_vkCmdCopyBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); uint64_t dst = r.read_handle();
        uint32_t count = r.read_u32();
        auto regions = r.template read_array<VkBufferCopy>(count);
        auto s = d.mapper_.get_buffer(src); auto d2 = d.mapper_.get_buffer(dst);
        if (cb && s && d2)
            vkCmdCopyBuffer(cb, s, d2, static_cast<uint32_t>(regions.size()), regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dst = r.read_handle(); VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        auto regions = r.template read_array<VkImageCopy>(count);
        auto s = d.mapper_.get_image(src); auto d2 = d.mapper_.get_image(dst);
        if (cb && s && d2) vkCmdCopyImage(cb, s, srcLayout, d2, dstLayout,
                                           static_cast<uint32_t>(regions.size()), regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdBlitImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dst = r.read_handle(); VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        auto regions = r.template read_array<VkImageBlit>(count);
        VkFilter filter = static_cast<VkFilter>(r.read_u32());
        auto s = d.mapper_.get_image(src);
        auto d2 = d.mapper_.get_image(dst);
        if (cb && s && d2)
            vkCmdBlitImage(cb, s, srcLayout, d2, dstLayout,
                           static_cast<uint32_t>(regions.size()), regions.data(), filter);
    });
    REGISTER(fbs::FunctionId_vkCmdCopyBufferToImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t srcBuf = r.read_handle(); uint64_t dstImg = r.read_handle();
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t rc = r.read_u32();
        std::vector<VkBufferImageCopy> regions(rc);
        for (auto& reg : regions) r.read_raw(&reg, sizeof(reg));
        VkBuffer src = d.mapper_.get_buffer(srcBuf);
        VkImage dst = d.mapper_.get_image(dstImg);
        if (cb && src && dst)
            vkCmdCopyBufferToImage(cb, src, dst, dstLayout, rc, regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImageToBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t srcImg = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dstBuf = r.read_handle();
        uint32_t rc = r.read_u32();
        std::vector<VkBufferImageCopy> regions(rc);
        for (auto& reg : regions) r.read_raw(&reg, sizeof(reg));
        VkImage src = d.mapper_.get_image(srcImg);
        VkBuffer dst = d.mapper_.get_buffer(dstBuf);
        if (cb && src && dst)
            vkCmdCopyImageToBuffer(cb, src, srcLayout, dst, rc, regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdUpdateBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t dst = r.read_handle();
        uint64_t off = r.read_u64();
        uint64_t size = r.read_u64();
        auto data = r.template read_array<uint8_t>(static_cast<uint32_t>(size));
        auto b = d.mapper_.get_buffer(dst);
        if (cb && b && data.size() > 0)
            vkCmdUpdateBuffer(cb, b, off, data.size(), data.data());
    });
    REGISTER(fbs::FunctionId_vkCmdFillBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t dst = r.read_handle(); uint64_t off = r.read_u64();
        uint64_t sz = r.read_u64(); uint32_t val = r.read_u32();
        auto b = d.mapper_.get_buffer(dst);
        if (cb && b) vkCmdFillBuffer(cb, b, off, sz, val);
    });
    REGISTER(fbs::FunctionId_vkCmdClearColorImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t img = r.read_handle();
        VkImageLayout layout = static_cast<VkImageLayout>(r.read_u32());
        VkClearColorValue color;
        r.read_raw(&color, sizeof(VkClearColorValue));
        uint32_t count = r.read_u32();
        auto ranges = r.template read_array<VkImageSubresourceRange>(count);
        auto i = d.mapper_.get_image(img);
        if (cb && i)
            vkCmdClearColorImage(cb, i, layout, &color,
                                 static_cast<uint32_t>(ranges.size()), ranges.data());
    });
    REGISTER(fbs::FunctionId_vkCmdClearDepthStencilImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkImage img = d.mapper_.get_image(r.read_handle());
        VkImageLayout layout = static_cast<VkImageLayout>(r.read_u32());
        VkClearDepthStencilValue ds{};
        r.read_raw(&ds, sizeof(ds));
        uint32_t rc = r.read_u32();
        std::vector<VkImageSubresourceRange> ranges(rc);
        for (auto& rg : ranges) r.read_raw(&rg, sizeof(rg));
        if (cb && img) vkCmdClearDepthStencilImage(cb, img, layout, &ds, rc, ranges.data());
    });
    REGISTER(fbs::FunctionId_vkCmdClearAttachments, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ac = r.read_u32();
        std::vector<VkClearAttachment> attachments(ac);
        for (uint32_t i = 0; i < ac; i++) {
            // VkClearAttachment has: aspectMask + colorAttachment + clearValue
            attachments[i].aspectMask = static_cast<VkImageAspectFlags>(r.read_u32());
            attachments[i].colorAttachment = r.read_u32();
            r.read_raw(&attachments[i].clearValue, sizeof(VkClearValue));
        }
        uint32_t rc = r.read_u32();
        std::vector<VkClearRect> rects(rc);
        for (auto& rect : rects) r.read_raw(&rect, sizeof(rect));
        if (cb) vkCmdClearAttachments(cb, ac, attachments.data(), rc, rects.data());
    });
    REGISTER(fbs::FunctionId_vkCmdResolveImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t rc = r.read_u32();
        std::vector<VkImageResolve> regions(rc);
        for (auto& reg : regions) r.read_raw(&reg, sizeof(reg));
        if (cb && src && dst) vkCmdResolveImage(cb, src, srcLayout, dst, dstLayout, rc, regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdExecuteCommands, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t count = r.read_u32();
        std::vector<VkCommandBuffer> secondaryCbs(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gCB = r.read_handle();
            secondaryCbs[i] = d.mapper_.get_command_buffer(gCB);
        }
        if (cb) vkCmdExecuteCommands(cb, count, secondaryCbs.data());
    });

    // --- Barrier ---
    REGISTER(fbs::FunctionId_vkCmdPipelineBarrier, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineStageFlags src = r.read_u32(); VkPipelineStageFlags dst = r.read_u32();
        VkDependencyFlags dep = r.read_u32();
        uint32_t mc = r.read_u32();
        std::vector<VkMemoryBarrier> memBarriers(mc);
        for (auto& mb : memBarriers) r.read_raw(&mb, sizeof(mb));

        uint32_t bc = r.read_u32();
        std::vector<VkBufferMemoryBarrier> bufBarriers(bc);
        for (auto& bb : bufBarriers) {
            r.read_raw(&bb, sizeof(bb));
            bb.buffer = d.mapper_.get_buffer(handle_to_u64(bb.buffer));
        }

        uint32_t ic = r.read_u32();
        std::vector<VkImageMemoryBarrier> imgBarriers(ic);
        for (auto& ib : imgBarriers) {
            r.read_raw(&ib, sizeof(ib));
            ib.image = d.mapper_.get_image(handle_to_u64(ib.image));
        }

        if (cb) vkCmdPipelineBarrier(cb, src, dst, dep,
            mc, memBarriers.data(),
            bc, bufBarriers.data(),
            ic, imgBarriers.data());
    });

    // --- Render Pass ---
    REGISTER(fbs::FunctionId_vkCmdBeginRenderPass, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkRenderPassBeginInfo bi{};
        read_VkRenderPassBeginInfo(r, &bi);
        r.read_u32(); // contents
        if (cb) {
            bi.renderPass = d.mapper_.get_render_pass(handle_to_u64(bi.renderPass));
            uint64_t gFB = handle_to_u64(bi.framebuffer);
            bi.framebuffer = d.mapper_.get_framebuffer(gFB);
            auto rtIt = d.framebufferRenderTarget_.find(gFB);
            if (rtIt != d.framebufferRenderTarget_.end()) {
                d.renderTargetImage_ = rtIt->second;
            } else {
                d.renderTargetImage_ = d.colorImage_;
            }
            vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        }
        delete[] bi.pClearValues;
    });
    REGISTER(fbs::FunctionId_vkCmdEndRenderPass, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdEndRenderPass(cb);
    });
    REGISTER(fbs::FunctionId_vkCmdNextSubpass, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle(); r.read_u32();
        if (cb) vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    });

    // --- Event commands ---
    REGISTER(fbs::FunctionId_vkCmdSetEvent, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle(); auto sm = r.read_u32();
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdSetEvent(cb, e, sm);
    });
    REGISTER(fbs::FunctionId_vkCmdResetEvent, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle(); auto sm = r.read_u32();
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdResetEvent(cb, e, sm);
    });
    REGISTER(fbs::FunctionId_vkCmdWaitEvents, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ec = r.read_u32();
        std::vector<uint64_t> events(ec);
        for (auto& e : events) e = r.read_handle();
        VkPipelineStageFlags srcStage = r.read_u32();
        VkPipelineStageFlags dstStage = r.read_u32();

        uint32_t mc = r.read_u32();
        std::vector<VkMemoryBarrier> memBarriers(mc);
        for (auto& mb : memBarriers) r.read_raw(&mb, sizeof(mb));

        uint32_t bc = r.read_u32();
        std::vector<VkBufferMemoryBarrier> bufBarriers(bc);
        for (auto& bb : bufBarriers) {
            r.read_raw(&bb, sizeof(bb));
            bb.buffer = d.mapper_.get_buffer(handle_to_u64(bb.buffer));
        }

        uint32_t ic = r.read_u32();
        std::vector<VkImageMemoryBarrier> imgBarriers(ic);
        for (auto& ib : imgBarriers) {
            r.read_raw(&ib, sizeof(ib));
            ib.image = d.mapper_.get_image(handle_to_u64(ib.image));
        }

        if (cb) {
            std::vector<VkEvent> hostEvents(ec);
            for (uint32_t i = 0; i < ec; i++)
                hostEvents[i] = d.mapper_.get_event(events[i]);
            vkCmdWaitEvents(cb, ec, hostEvents.data(), srcStage, dstStage,
                mc, memBarriers.data(),
                bc, bufBarriers.data(),
                ic, imgBarriers.data());
        }
    });

    // --- Query commands ---
    REGISTER(fbs::FunctionId_vkCmdBeginQuery, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        VkQueryControlFlags flags = static_cast<VkQueryControlFlags>(r.read_u32());
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdBeginQuery(cb, qph, q, flags);
    });
    REGISTER(fbs::FunctionId_vkCmdEndQuery, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdEndQuery(cb, qph, q);
    });
    REGISTER(fbs::FunctionId_vkCmdWriteTimestamp, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineStageFlags stage = static_cast<VkPipelineStageFlags>(r.read_u32());
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdWriteTimestamp(cb, static_cast<VkPipelineStageFlagBits>(stage), qph, q);
    });
    REGISTER(fbs::FunctionId_vkCmdResetQueryPool, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t fq = r.read_u32(); uint32_t qc = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdResetQueryPool(cb, qph, fq, qc);
    });
    REGISTER(fbs::FunctionId_vkCmdCopyQueryPoolResults, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t gPool = r.read_handle();
        uint32_t firstQuery = r.read_u32();
        uint32_t queryCount = r.read_u32();
        uint64_t gDstBuf = r.read_handle();
        VkDeviceSize dstOffset = r.read_u64();
        VkDeviceSize stride = r.read_u64();
        VkQueryResultFlags flags = static_cast<VkQueryResultFlags>(r.read_u32());
        VkQueryPool pool = d.mapper_.get_query_pool(gPool);
        VkBuffer dstBuf = d.mapper_.get_buffer(gDstBuf);
        if (cb && pool && dstBuf)
            vkCmdCopyQueryPoolResults(cb, pool, firstQuery, queryCount,
                                       dstBuf, dstOffset, stride, flags);
    });
    REGISTER(fbs::FunctionId_vkGetQueryPoolResults, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle();
        uint64_t gPool = r.read_handle();
        uint32_t firstQuery = r.read_u32();
        uint32_t queryCount = r.read_u32();
        uint64_t dataSize = r.read_u64();
        uint64_t stride = r.read_u64();
        VkQueryResultFlags flags = static_cast<VkQueryResultFlags>(r.read_u32());

        VkQueryPool pool = d.mapper_.get_query_pool(gPool);
        std::vector<uint8_t> data(dataSize, 0);
        if (dev && pool && dataSize > 0) {
            VkResult res = vkGetQueryPoolResults(dev, pool, firstQuery, queryCount,
                                                 static_cast<size_t>(dataSize), data.data(),
                                                 static_cast<VkDeviceSize>(stride), flags);
            if (res == VK_SUCCESS && d.sendDataFn_) {
                d.sendDataFn_(gPool, data.data(), data.size(), 0);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkGetImageSubresourceLayout, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle();
        uint64_t gImg = r.read_handle();
        VkImageSubresource sub{};
        r.read_raw(&sub, sizeof(sub));

        VkImage img = d.mapper_.get_image(gImg);
        VkSubresourceLayout layout{};
        if (dev && img) {
            vkGetImageSubresourceLayout(dev, img, &sub, &layout);
            if (d.sendDataFn_) {
                d.sendDataFn_(gImg, reinterpret_cast<const uint8_t*>(&layout), sizeof(layout), 0);
            }
        }
    });

    // --- Dynamic state (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdSetCullMode, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkCullModeFlags cm = r.read_u32();
        if (cb) vkCmdSetCullMode(cb, cm);
    });
    REGISTER(fbs::FunctionId_vkCmdSetFrontFace, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkFrontFace ff = static_cast<VkFrontFace>(r.read_u32());
        if (cb) vkCmdSetFrontFace(cb, ff);
    });
    REGISTER(fbs::FunctionId_vkCmdSetPrimitiveTopology, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPrimitiveTopology pt = static_cast<VkPrimitiveTopology>(r.read_u32());
        if (cb) vkCmdSetPrimitiveTopology(cb, pt);
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthTestEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetDepthTestEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthWriteEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetDepthWriteEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthCompareOp, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetDepthCompareOp(cb, static_cast<VkCompareOp>(r.read_u32()));
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthBoundsTestEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetDepthBoundsTestEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetStencilTestEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetStencilTestEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetStencilOp, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkStencilFaceFlags fm = r.read_u32();
        VkStencilOp fo = static_cast<VkStencilOp>(r.read_u32());
        VkStencilOp po = static_cast<VkStencilOp>(r.read_u32());
        VkStencilOp dfo = static_cast<VkStencilOp>(r.read_u32());
        VkCompareOp co = static_cast<VkCompareOp>(r.read_u32());
        if (cb) vkCmdSetStencilOp(cb, fm, fo, po, dfo, co);
    });
    REGISTER(fbs::FunctionId_vkCmdSetRasterizerDiscardEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetRasterizerDiscardEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthBiasEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetDepthBiasEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetPrimitiveRestartEnable, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdSetPrimitiveRestartEnable(cb, r.read_bool());
    });
    REGISTER(fbs::FunctionId_vkCmdSetViewportWithCount, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t count = r.read_u32();
        auto vps = r.template read_array<VkViewport>(count);
        if (cb) vkCmdSetViewportWithCount(cb, count, vps.data());
    });
    REGISTER(fbs::FunctionId_vkCmdSetScissorWithCount, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t count = r.read_u32();
        auto scis = r.template read_array<VkRect2D>(count);
        if (cb) vkCmdSetScissorWithCount(cb, count, scis.data());
    });

    // --- Synchronization2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdPipelineBarrier2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkDependencyFlags depFlags = static_cast<VkDependencyFlags>(r.read_u32());
        uint32_t mem_br = r.read_u32();
        std::vector<VkMemoryBarrier2> memBarriers2(mem_br);
        for (auto& mb : memBarriers2) {
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            mb.pNext = nullptr;
            mb.srcStageMask = r.read_u64();
            mb.srcAccessMask = r.read_u64();
            mb.dstStageMask = r.read_u64();
            mb.dstAccessMask = r.read_u64();
        }

        uint32_t buf_br = r.read_u32();
        std::vector<VkBufferMemoryBarrier2> bufBarriers2(buf_br);
        for (auto& bb : bufBarriers2) {
            bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            bb.pNext = nullptr;
            bb.srcStageMask = r.read_u64();
            bb.srcAccessMask = r.read_u64();
            bb.dstStageMask = r.read_u64();
            bb.dstAccessMask = r.read_u64();
            bb.srcQueueFamilyIndex = r.read_u32();
            bb.dstQueueFamilyIndex = r.read_u32();
            bb.buffer = d.mapper_.get_buffer(r.read_handle());
            bb.offset = r.read_u64();
            bb.size = r.read_u64();
        }

        uint32_t img_br = r.read_u32();
        std::vector<VkImageMemoryBarrier2> imgBarriers2(img_br);
        for (auto& ib : imgBarriers2) {
            ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            ib.pNext = nullptr;
            ib.srcStageMask = r.read_u64();
            ib.srcAccessMask = r.read_u64();
            ib.dstStageMask = r.read_u64();
            ib.dstAccessMask = r.read_u64();
            ib.oldLayout = static_cast<VkImageLayout>(r.read_u32());
            ib.newLayout = static_cast<VkImageLayout>(r.read_u32());
            ib.srcQueueFamilyIndex = r.read_u32();
            ib.dstQueueFamilyIndex = r.read_u32();
            ib.image = d.mapper_.get_image(r.read_handle());
            r.read_raw(&ib.subresourceRange, sizeof(VkImageSubresourceRange));
        }

        VkDependencyInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.dependencyFlags = depFlags;
        di.memoryBarrierCount = mem_br;
        di.pMemoryBarriers = memBarriers2.data();
        di.bufferMemoryBarrierCount = buf_br;
        di.pBufferMemoryBarriers = bufBarriers2.data();
        di.imageMemoryBarrierCount = img_br;
        di.pImageMemoryBarriers = imgBarriers2.data();
        if (cb) {
            auto pfnCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdPipelineBarrier2"));
            if (pfnCmdPipelineBarrier2) {
                pfnCmdPipelineBarrier2(cb, &di);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdBeginRendering, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkRenderingInfo ri{};
        read_VkRenderingInfo(r, &ri);
        if (cb) {
            for (uint32_t i = 0; i < ri.colorAttachmentCount && ri.pColorAttachments; i++) {
                auto& att = const_cast<VkRenderingAttachmentInfo&>(ri.pColorAttachments[i]);
                uint64_t gView = handle_to_u64(att.imageView);
                att.imageView = d.mapper_.get_image_view(gView);
                att.resolveImageView = d.mapper_.get_image_view(handle_to_u64(att.resolveImageView));
                // Track render target from first color attachment
                if (i == 0) {
                    VkImage img = d.mapper_.get_view_image(gView);
                    if (img != VK_NULL_HANDLE)
                        d.renderTargetImage_ = img;
                }
            }
            if (ri.pDepthAttachment) {
                auto& da = const_cast<VkRenderingAttachmentInfo&>(*ri.pDepthAttachment);
                da.imageView = d.mapper_.get_image_view(handle_to_u64(da.imageView));
                da.resolveImageView = d.mapper_.get_image_view(handle_to_u64(da.resolveImageView));
            }
            if (ri.pStencilAttachment) {
                auto& sa = const_cast<VkRenderingAttachmentInfo&>(*ri.pStencilAttachment);
                sa.imageView = d.mapper_.get_image_view(handle_to_u64(sa.imageView));
                sa.resolveImageView = d.mapper_.get_image_view(handle_to_u64(sa.resolveImageView));
            }
            if (d.renderTargetImage_ == VK_NULL_HANDLE)
                d.renderTargetImage_ = d.colorImage_;
            vkCmdBeginRendering(cb, &ri);
        }
    });
    REGISTER(fbs::FunctionId_vkCmdEndRendering, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdEndRendering(cb);
    });

    // --- Copy commands 2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdCopyBuffer2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle(); // cmdBuffer
        VkBuffer src = d.mapper_.get_buffer(r.read_handle());
        VkBuffer dst = d.mapper_.get_buffer(r.read_handle());
        uint32_t count = r.read_u32();
        std::vector<VkBufferCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkBufferCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyBufferInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        ci.pNext = nullptr;
        ci.srcBuffer = src;
        ci.dstBuffer = dst;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyBuffer2 = reinterpret_cast<PFN_vkCmdCopyBuffer2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyBuffer2"));
            if (pfnCmdCopyBuffer2) {
                pfnCmdCopyBuffer2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        std::vector<VkImageCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkImageCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyImage2 = reinterpret_cast<PFN_vkCmdCopyImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyImage2"));
            if (pfnCmdCopyImage2) {
                pfnCmdCopyImage2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdCopyBufferToImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkBuffer src = d.mapper_.get_buffer(r.read_handle());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        std::vector<VkBufferImageCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkBufferImageCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyBufferToImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcBuffer = src;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyBufferToImage2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyBufferToImage2"));
            if (pfnCmdCopyBufferToImage2) {
                pfnCmdCopyBufferToImage2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImageToBuffer2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkBuffer dst = d.mapper_.get_buffer(r.read_handle());
        uint32_t count = r.read_u32();
        std::vector<VkBufferImageCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkBufferImageCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyImageToBufferInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstBuffer = dst;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyImageToBuffer2 = reinterpret_cast<PFN_vkCmdCopyImageToBuffer2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyImageToBuffer2"));
            if (pfnCmdCopyImageToBuffer2) {
                pfnCmdCopyImageToBuffer2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdResolveImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        std::vector<VkImageResolve2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkImageResolve2));
            regions[i].pNext = nullptr;
        }

        VkResolveImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdResolveImage2 = reinterpret_cast<PFN_vkCmdResolveImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdResolveImage2"));
            if (pfnCmdResolveImage2) {
                pfnCmdResolveImage2(cb, &ci);
            }
        }
    });

    // --- QueueSubmit2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkQueueSubmit2, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        VkQueue q = d.mapper_.queue();
        r.read_handle();
        uint32_t count = r.read_u32();

        std::vector<VkSubmitInfo2> submits(count);
        for (uint32_t i = 0; i < count; i++) {
            if (!read_VkSubmitInfo2(r, &submits[i])) break;
            auto& si = submits[i];
            for (uint32_t j = 0; j < si.commandBufferInfoCount; j++) {
                auto& cmdInfo = const_cast<VkCommandBufferSubmitInfo&>(si.pCommandBufferInfos[j]);
                cmdInfo.pNext = nullptr;
                cmdInfo.commandBuffer = d.mapper_.get_command_buffer(
                    handle_to_u64(cmdInfo.commandBuffer));
            }
            for (uint32_t j = 0; j < si.waitSemaphoreInfoCount; j++) {
                auto& semInfo = const_cast<VkSemaphoreSubmitInfo&>(si.pWaitSemaphoreInfos[j]);
                semInfo.pNext = nullptr;
                semInfo.semaphore = d.mapper_.get_semaphore(
                    handle_to_u64(semInfo.semaphore));
            }
            for (uint32_t j = 0; j < si.signalSemaphoreInfoCount; j++) {
                auto& semInfo = const_cast<VkSemaphoreSubmitInfo&>(si.pSignalSemaphoreInfos[j]);
                semInfo.pNext = nullptr;
                semInfo.semaphore = d.mapper_.get_semaphore(
                    handle_to_u64(semInfo.semaphore));
            }
        }

        uint64_t guestFence = r.read_handle();
        VkFence fence = d.mapper_.get_fence(guestFence);
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        bool ownFence = false;
        if (fence == VK_NULL_HANDLE) {
            if (vkCreateFence(dev, &fci, nullptr, &fence) == VK_SUCCESS)
                ownFence = true;
        }

        auto pfnQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(
            vkGetDeviceProcAddr(dev, "vkQueueSubmit2"));
        VkResult res = pfnQueueSubmit2
            ? pfnQueueSubmit2(q, count, submits.data(), fence)
            : VK_ERROR_EXTENSION_NOT_PRESENT;
        if (res == VK_SUCCESS) {
            d.pendingSubmitFence_ = fence;
            d.hasPendingSubmit_ = true;
            SPDLOG_DEBUG("  vkQueueSubmit2 ({} submits) -> submitted, fence={}",
                         count, (void*)fence);
        } else {
            SPDLOG_ERROR("  vkQueueSubmit2 failed: {}", static_cast<int>(res));
            if (ownFence) vkDestroyFence(dev, fence, nullptr);
        }

        for (uint32_t i = 0; i < count; i++)
            free_VkSubmitInfo2(&submits[i]);
    });

    // --- Private Data (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCreatePrivateDataSlot, [](auto& d, auto& r) {
        r.read_handle();
        VkPrivateDataSlotCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSlot = r.read_handle();
        VkPrivateDataSlot slot;
        if (vkCreatePrivateDataSlot(d.mapper_.device(), &ci, nullptr, &slot) == VK_SUCCESS) {
            d.mapper_.store_private_data_slot(pSlot, slot);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPrivateDataSlot, [](auto& d, auto& r) {
        r.read_handle(); uint64_t gSlot = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto slot = d.mapper_.get_private_data_slot(gSlot);
        if (slot) vkDestroyPrivateDataSlot(d.mapper_.device(), slot, nullptr);
        d.mapper_.remove_private_data_slot(gSlot);
    });
    REGISTER(fbs::FunctionId_vkSetPrivateData, [](auto& d, auto& r) {
        r.read_handle(); r.read_u32(); r.read_u64(); r.read_handle(); r.read_u64();
    });

    // --- Swapchain (KHR) - Offscreen only ---
    REGISTER(fbs::FunctionId_vkCreateSwapchainKHR, [](auto& d, auto& r) {
        r.read_handle();
        VkSwapchainCreateInfoKHR ci{};
        read_VkSwapchainCreateInfoKHR(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        r.read_handle();
        delete[] ci.pQueueFamilyIndices;
    });
    REGISTER(fbs::FunctionId_vkDestroySwapchainKHR, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
    });
    REGISTER(fbs::FunctionId_vkGetSwapchainImagesKHR, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_handle(); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkAcquireNextImageKHR, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_u64(); r.read_handle();
        r.read_handle(); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkAcquireNextImage2KHR, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkAcquireNextImageInfoKHR)); r.read_handle();
    });

    // --- Descriptor Update Template (1.1 promoted) ---
    REGISTER(fbs::FunctionId_vkCreateDescriptorUpdateTemplate, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkDescriptorUpdateTemplateCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
        ci.flags = r.read_u32();
        ci.descriptorUpdateEntryCount = r.read_u32();
        std::vector<VkDescriptorUpdateTemplateEntry> entries(ci.descriptorUpdateEntryCount);
        for (uint32_t i = 0; i < ci.descriptorUpdateEntryCount; i++) {
            entries[i].dstBinding = r.read_u32();
            entries[i].dstArrayElement = r.read_u32();
            entries[i].descriptorCount = r.read_u32();
            entries[i].descriptorType = static_cast<VkDescriptorType>(r.read_u32());
            entries[i].offset = static_cast<size_t>(r.read_u64());
            entries[i].stride = static_cast<size_t>(r.read_u64());
        }
        ci.pDescriptorUpdateEntries = entries.data();
        ci.templateType = static_cast<VkDescriptorUpdateTemplateType>(r.read_u32());
        ci.descriptorSetLayout = d.mapper_.get_dsl(r.read_handle());
        ci.pipelineBindPoint = static_cast<VkPipelineBindPoint>(r.read_u32());
        ci.pipelineLayout = d.mapper_.get_pipeline_layout(r.read_handle());
        ci.set = r.read_u32();
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pTpl = r.read_handle();
        VkDescriptorUpdateTemplate tpl;
        if (vkCreateDescriptorUpdateTemplate(d.mapper_.device(), &ci, nullptr, &tpl) == VK_SUCCESS) {
            d.mapper_.store_descriptor_update_template(pTpl, tpl);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorUpdateTemplate, [](auto& d, auto& r) {
        r.read_handle(); auto t = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto tpl = d.mapper_.get_descriptor_update_template(t);
        if (tpl) vkDestroyDescriptorUpdateTemplate(d.mapper_.device(), tpl, nullptr);
        d.mapper_.remove_descriptor_update_template(t);
    });
    REGISTER(fbs::FunctionId_vkUpdateDescriptorSetWithTemplate, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t gDS = r.read_handle();
        uint64_t gTpl = r.read_handle();
        auto ds = d.mapper_.get_ds(gDS);
        auto tpl = d.mapper_.get_descriptor_update_template(gTpl);
        uint64_t remaining = r.remaining();
        std::vector<uint8_t> data(remaining);
        if (remaining > 0) r.read_raw(data.data(), remaining);
        if (dev && ds && tpl) {
            vkUpdateDescriptorSetWithTemplate(dev, ds, tpl, data.data());
        }
    });

    // --- Sampler YCbCr (1.1 promoted) ---
    REGISTER(fbs::FunctionId_vkCreateSamplerYcbcrConversion, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkSamplerYcbcrConversionCreateInfo));
        r.skip(sizeof(VkAllocationCallbacks)); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkDestroySamplerYcbcrConversion, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
    });

    // --- Bind sparse ---
    REGISTER(fbs::FunctionId_vkQueueBindSparse, [](auto& d, auto& r) {
        r.read_handle(); uint32_t c = r.read_u32();
        r.skip(c * sizeof(VkBindSparseInfo)); r.read_handle();
    });

    // --- Device groups ---
    REGISTER(fbs::FunctionId_vkCmdSetDeviceMask, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle(); r.read_u32();
        if (cb) vkCmdSetDeviceMask(cb, 1);
    });
    REGISTER(fbs::FunctionId_vkGetDeviceGroupPeerMemoryFeatures, [](auto& d, auto& r) {
        r.read_handle(); r.read_u32(); r.read_u32(); r.read_u32(); r.read_handle();
    });

    // --- Dispatch base ---
    REGISTER(fbs::FunctionId_vkCmdDispatchBase, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_u32(); r.read_u32(); r.read_u32(); // baseGroupX/Y/Z
        uint32_t x = r.read_u32(); uint32_t y = r.read_u32(); uint32_t z = r.read_u32();
        if (cb) vkCmdDispatchBase(cb, 0, 0, 0, x, y, z);
    });

    // --- Vertex input EXT ---
    REGISTER(fbs::FunctionId_vkCmdSetVertexInputEXT, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t bc = r.read_u32();
        std::vector<VkVertexInputBindingDescription2EXT> bindings(bc);
        for (auto& b : bindings) r.read_raw(&b, sizeof(b));

        uint32_t ac = r.read_u32();
        std::vector<VkVertexInputAttributeDescription2EXT> attrs(ac);
        for (auto& a : attrs) r.read_raw(&a, sizeof(a));

        if (cb) {
            auto func = reinterpret_cast<PFN_vkCmdSetVertexInputEXT>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdSetVertexInputEXT"));
            if (func)
                func(cb, bc, bindings.data(), ac, attrs.data());
        }
    });

    // --- Memory requirements (3) ---
    REGISTER(fbs::FunctionId_vkGetDeviceBufferMemoryRequirements, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceBufferMemoryRequirements));
        r.skip(sizeof(VkMemoryRequirements2));
    });
    REGISTER(fbs::FunctionId_vkGetDeviceImageMemoryRequirements, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceImageMemoryRequirements));
        r.skip(sizeof(VkMemoryRequirements2));
    });
    REGISTER(fbs::FunctionId_vkGetDeviceImageSparseMemoryRequirements, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceImageMemoryRequirements));
        r.read_handle(); r.read_handle();
    });

    // --- Device group present ---
    REGISTER(fbs::FunctionId_vkGetDeviceGroupPresentCapabilitiesKHR, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceGroupPresentCapabilitiesKHR));
    });
    REGISTER(fbs::FunctionId_vkGetDeviceGroupSurfacePresentModesKHR, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_handle();
    });

    // --- Bind buffer/image 2 (1.1) ---
    REGISTER(fbs::FunctionId_vkBindBufferMemory2, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkBindBufferMemoryInfo> infos(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&infos[i], sizeof(VkBindBufferMemoryInfo));
            infos[i].pNext = nullptr;
            infos[i].buffer = d.mapper_.get_buffer(handle_to_u64(infos[i].buffer));
            infos[i].memory = d.mapper_.get_device_memory(handle_to_u64(infos[i].memory));
        }
        if (dev && count > 0) {
            auto pfnBindBufferMemory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(
                vkGetDeviceProcAddr(dev, "vkBindBufferMemory2"));
            if (pfnBindBufferMemory2) {
                pfnBindBufferMemory2(dev, count, infos.data());
            }
        }
    });
    REGISTER(fbs::FunctionId_vkBindImageMemory2, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkBindImageMemoryInfo> infos(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&infos[i], sizeof(VkBindImageMemoryInfo));
            infos[i].pNext = nullptr;
            infos[i].image = d.mapper_.get_image(handle_to_u64(infos[i].image));
            infos[i].memory = d.mapper_.get_device_memory(handle_to_u64(infos[i].memory));
        }
        if (dev && count > 0) {
            auto pfnBindImageMemory2 = reinterpret_cast<PFN_vkBindImageMemory2>(
                vkGetDeviceProcAddr(dev, "vkBindImageMemory2"));
            if (pfnBindImageMemory2) {
                pfnBindImageMemory2(dev, count, infos.data());
            }
        }
    });
    REGISTER(fbs::FunctionId_vkTrimCommandPool, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_u32();
    });
    REGISTER(fbs::FunctionId_vkMergePipelineCaches, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); uint32_t c = r.read_u32();
        r.skip(c * sizeof(uint64_t));
    });

    // --- Draw indirect count (1.2) ---
    REGISTER(fbs::FunctionId_vkCmdDrawIndirectCount, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkBuffer buf = d.mapper_.get_buffer(r.read_handle());
        VkDeviceSize offset = r.read_u64();
        VkBuffer countBuf = d.mapper_.get_buffer(r.read_handle());
        VkDeviceSize countOffset = r.read_u64();
        uint32_t maxDrawCount = r.read_u32();
        uint32_t stride = r.read_u32();
        if (cb && buf && countBuf) {
            vkCmdDrawIndirectCount(cb, buf, offset, countBuf, countOffset, maxDrawCount, stride);
        }
    });
    REGISTER(fbs::FunctionId_vkCmdDrawIndexedIndirectCount, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkBuffer buf = d.mapper_.get_buffer(r.read_handle());
        VkDeviceSize offset = r.read_u64();
        VkBuffer countBuf = d.mapper_.get_buffer(r.read_handle());
        VkDeviceSize countOffset = r.read_u64();
        uint32_t maxDrawCount = r.read_u32();
        uint32_t stride = r.read_u32();
        if (cb && buf && countBuf) {
            vkCmdDrawIndexedIndirectCount(cb, buf, offset, countBuf, countOffset, maxDrawCount, stride);
        }
    });

    // --- Render pass 2 (1.2) ---
    REGISTER(fbs::FunctionId_vkCmdBeginRenderPass2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkRenderPassBeginInfo bi{};
        read_VkRenderPassBeginInfo(r, &bi);
        r.skip(sizeof(VkSubpassBeginInfo));
        if (cb) {
            bi.renderPass = d.mapper_.get_render_pass(handle_to_u64(bi.renderPass));
            uint64_t gFB = handle_to_u64(bi.framebuffer);
            bi.framebuffer = d.mapper_.get_framebuffer(gFB);
            auto rtIt = d.framebufferRenderTarget_.find(gFB);
            if (rtIt != d.framebufferRenderTarget_.end()) {
                d.renderTargetImage_ = rtIt->second;
            } else {
                d.renderTargetImage_ = d.colorImage_;
            }
            vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        }
        delete[] bi.pClearValues;
    });
    REGISTER(fbs::FunctionId_vkCmdEndRenderPass2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkSubpassEndInfo));
        if (cb) vkCmdEndRenderPass(cb);
    });
    REGISTER(fbs::FunctionId_vkCmdNextSubpass2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkSubpassBeginInfo) + sizeof(VkSubpassEndInfo));
        if (cb) vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    });

    // --- Wait/Signal semaphore (1.2) ---
    REGISTER(fbs::FunctionId_vkWaitSemaphores, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        VkSemaphoreWaitInfo wi{};
        read_VkSemaphoreWaitInfo(r, &wi);
        uint64_t timeout = r.read_u64();
        // Remap semaphore handles
        for (uint32_t i = 0; i < wi.semaphoreCount; i++)
            const_cast<VkSemaphore*>(wi.pSemaphores)[i] =
                d.mapper_.get_semaphore(handle_to_u64(wi.pSemaphores[i]));
        VkResult res = vkWaitSemaphores(dev, &wi, timeout);
        if (res != VK_SUCCESS)
            SPDLOG_WARN("vkWaitSemaphores: result={}", static_cast<int>(res));
        free_VkSemaphoreWaitInfo(&wi);
    });
    REGISTER(fbs::FunctionId_vkSignalSemaphore, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        VkSemaphoreSignalInfo si{};
        read_VkSemaphoreSignalInfo(r, &si);
        si.semaphore = d.mapper_.get_semaphore(handle_to_u64(si.semaphore));
        vkSignalSemaphore(dev, &si);
    });
    REGISTER(fbs::FunctionId_vkGetSemaphoreCounterValue, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        VkSemaphore sem = d.mapper_.get_semaphore(r.read_handle());
        r.read_handle(); // pValue output pointer (ignore)
        uint64_t val = 0;
        vkGetSemaphoreCounterValue(dev, sem, &val);
    });

    // --- Reset query pool (1.2) ---
    REGISTER(fbs::FunctionId_vkResetQueryPool, [](auto& d, auto& r) {
        r.read_handle(); auto qp = r.read_handle();
        uint32_t firstQuery = r.read_u32(); uint32_t queryCount = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (qph) vkResetQueryPool(d.mapper_.device(), qph, firstQuery, queryCount);
    });

    // --- Bind vertex buffers 2 (1.3) ---
    REGISTER(fbs::FunctionId_vkCmdBindVertexBuffers2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32();
        uint32_t count = r.read_u32();
        auto bufs = r.template read_array<uint64_t>(count);
        auto offs = r.template read_array<uint64_t>(count);
        auto sizes = r.template read_array<uint64_t>(count);
        auto strides = r.template read_array<uint64_t>(count);
        if (cb && bufs.size() > 0) {
            std::vector<VkBuffer> hbufs;
            for (auto& g : bufs) hbufs.push_back(d.mapper_.get_buffer(g));
            vkCmdBindVertexBuffers2(cb, first, static_cast<uint32_t>(hbufs.size()),
                                    hbufs.data(), offs.data(), sizes.data(), strides.data());
        }
    });

    // --- Reset/Set event 2 (1.3) ---
    REGISTER(fbs::FunctionId_vkCmdResetEvent2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle(); r.read_u64(); // stageMask2
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdResetEvent2(cb, e, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    });
    REGISTER(fbs::FunctionId_vkCmdSetEvent2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle();
        auto e = d.mapper_.get_event(evt);
        VkDependencyInfo di{};
        read_VkDependencyInfo(r, &di);
        // Remap buffer and image handles
        for (uint32_t j = 0; j < di.bufferMemoryBarrierCount; j++)
            const_cast<VkBufferMemoryBarrier2*>(di.pBufferMemoryBarriers)[j].buffer =
                d.mapper_.get_buffer(handle_to_u64(di.pBufferMemoryBarriers[j].buffer));
        for (uint32_t j = 0; j < di.imageMemoryBarrierCount; j++)
            const_cast<VkImageMemoryBarrier2*>(di.pImageMemoryBarriers)[j].image =
                d.mapper_.get_image(handle_to_u64(di.pImageMemoryBarriers[j].image));
        if (cb && e) vkCmdSetEvent2(cb, e, &di);
        free_VkDependencyInfo(&di);
    });
    REGISTER(fbs::FunctionId_vkCmdWaitEvents2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ec = r.read_u32();
        std::vector<VkEvent> hostEvents(ec);
        for (uint32_t i = 0; i < ec; i++)
            hostEvents[i] = d.mapper_.get_event(r.read_handle());

        std::vector<VkDependencyInfo> depInfos(ec);
        for (uint32_t i = 0; i < ec; i++) {
            read_VkDependencyInfo(r, &depInfos[i]);
            for (uint32_t j = 0; j < depInfos[i].bufferMemoryBarrierCount; j++)
                const_cast<VkBufferMemoryBarrier2*>(depInfos[i].pBufferMemoryBarriers)[j].buffer =
                    d.mapper_.get_buffer(handle_to_u64(depInfos[i].pBufferMemoryBarriers[j].buffer));
            for (uint32_t j = 0; j < depInfos[i].imageMemoryBarrierCount; j++)
                const_cast<VkImageMemoryBarrier2*>(depInfos[i].pImageMemoryBarriers)[j].image =
                    d.mapper_.get_image(handle_to_u64(depInfos[i].pImageMemoryBarriers[j].image));
        }

        if (cb) {
            auto func = reinterpret_cast<PFN_vkCmdWaitEvents2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdWaitEvents2"));
            if (func)
                func(cb, ec, hostEvents.data(), depInfos.data());
        }
        for (auto& di : depInfos) free_VkDependencyInfo(&di);
    });
    REGISTER(fbs::FunctionId_vkCmdWriteTimestamp2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_u64(); uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, qph, q);
    });

    // --- Blit / Resolve / CopyBufferToImage 2 (1.3) ---
    REGISTER(fbs::FunctionId_vkCmdBlitImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t regionCount = r.read_u32();
        std::vector<VkImageBlit2> regions(regionCount);
        for (uint32_t i = 0; i < regionCount; i++) {
            r.read_raw(&regions[i], sizeof(VkImageBlit2));
            regions[i].pNext = nullptr;
        }
        VkFilter filter = static_cast<VkFilter>(r.read_u32());

        VkBlitImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = regionCount;
        ci.pRegions = regions.data();
        ci.filter = filter;
        if (cb && src && dst) {
            auto pfn = reinterpret_cast<PFN_vkCmdBlitImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdBlitImage2"));
            if (pfn) pfn(cb, &ci);
        }
    });
    REGISTER(fbs::FunctionId_vkCmdDispatchIndirect, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t buf = r.read_handle(); uint64_t off = r.read_u64();
        auto b = d.mapper_.get_buffer(buf);
        if (cb && b) vkCmdDispatchIndirect(cb, b, off);
    });
    REGISTER(fbs::FunctionId_vkCmdSetDepthBounds, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        float mn = r.read_f32(); float mx = r.read_f32();
        if (cb) vkCmdSetDepthBounds(cb, mn, mx);
    });

    // --- Viewport/Scissor with count (1.3) already handled above ---

    #undef REGISTER
}

CommandDispatcher::~CommandDispatcher() {
    mapper_.cleanup();
    teardown_framebuffer();
}

void CommandDispatcher::cleanup() {
    mapper_.cleanup();
    teardown_framebuffer();
    // Reset device so destructor won't re-destroy with stale handle
    mapper_.set_device(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
}

void CommandDispatcher::set_device(VkPhysicalDevice physDev, VkDevice device,
                                    VkQueue queue, uint32_t queueFamily,
                                    VkCommandPool cmdPool) {
    mapper_.set_device(device, queue, queueFamily, cmdPool);
    physDev_ = physDev;
    cache_device_procs(device);
}

void CommandDispatcher::set_framebuffer_size(uint32_t w, uint32_t h) {
    fbWidth_ = w;
    fbHeight_ = h;
}

bool CommandDispatcher::setup_framebuffer() {
    SPDLOG_INFO("setup_framebuffer: starting, size={}x{}", fbWidth_, fbHeight_);
    
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = {fbWidth_, fbHeight_, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    SPDLOG_INFO("setup_framebuffer: creating image, device={}", (void*)mapper_.device());
    VkResult res = vkCreateImage(mapper_.device(), &imgInfo, nullptr, &colorImage_);
    SPDLOG_INFO("setup_framebuffer: vkCreateImage res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;

    SPDLOG_INFO("setup_framebuffer: getting image memory requirements");
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(mapper_.device(), colorImage_, &memReq);
    SPDLOG_INFO("setup_framebuffer: memReq size={}, typeBits={}", memReq.size, memReq.memoryTypeBits);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    
    SPDLOG_INFO("setup_framebuffer: getting physical device memory properties, physDev={}", (void*)physDev_);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev_, &memProps);
    
    allocInfo.memoryTypeIndex = 0;
    bool found = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            allocInfo.memoryTypeIndex = i;
            found = true;
            break;
        }
    }
    if (!found) {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if (memReq.memoryTypeBits & (1 << i)) {
                allocInfo.memoryTypeIndex = i;
                break;
            }
        }
    }
    SPDLOG_INFO("setup_framebuffer: allocating memory, typeIndex={}", allocInfo.memoryTypeIndex);
    res = vkAllocateMemory(mapper_.device(), &allocInfo, nullptr, &colorMemory_);
    SPDLOG_INFO("setup_framebuffer: vkAllocateMemory res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;
        
    SPDLOG_INFO("setup_framebuffer: binding image memory");
    vkBindImageMemory(mapper_.device(), colorImage_, colorMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    
    SPDLOG_INFO("setup_framebuffer: creating image view");
    res = vkCreateImageView(mapper_.device(), &viewInfo, nullptr, &colorView_);
    SPDLOG_INFO("setup_framebuffer: vkCreateImageView res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    
    SPDLOG_INFO("setup_framebuffer: creating render pass");
    res = vkCreateRenderPass(mapper_.device(), &rpInfo, nullptr, &renderPass_);
    SPDLOG_INFO("setup_framebuffer: vkCreateRenderPass res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &colorView_;
    fbInfo.width = fbWidth_;
    fbInfo.height = fbHeight_;
    fbInfo.layers = 1;
    
    SPDLOG_INFO("setup_framebuffer: creating framebuffer");
    res = vkCreateFramebuffer(mapper_.device(), &fbInfo, nullptr, &mainFramebuffer_);
    SPDLOG_INFO("setup_framebuffer: vkCreateFramebuffer res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;

    // Readback buffer
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = fbWidth_ * fbHeight_ * 4;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    SPDLOG_INFO("setup_framebuffer: creating readback buffer");
    res = vkCreateBuffer(mapper_.device(), &bufInfo, nullptr, &readbackBuffer_);
    SPDLOG_INFO("setup_framebuffer: vkCreateBuffer res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;

    VkMemoryRequirements rbMemReq;
    vkGetBufferMemoryRequirements(mapper_.device(), readbackBuffer_, &rbMemReq);

    VkMemoryAllocateInfo rbAlloc{};
    rbAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    rbAlloc.allocationSize = rbMemReq.size;
    found = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((rbMemReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            rbAlloc.memoryTypeIndex = i;
            found = true;
            break;
        }
    }
    if (!found) {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if (rbMemReq.memoryTypeBits & (1 << i)) {
                rbAlloc.memoryTypeIndex = i;
                break;
            }
        }
    }
    
    SPDLOG_INFO("setup_framebuffer: allocating readback memory, typeIndex={}", rbAlloc.memoryTypeIndex);
    res = vkAllocateMemory(mapper_.device(), &rbAlloc, nullptr, &readbackMemory_);
    SPDLOG_INFO("setup_framebuffer: vkAllocateMemory res={}", (int)res);
    if (res != VK_SUCCESS)
        return false;
        
    SPDLOG_INFO("setup_framebuffer: binding readback buffer memory");
    vkBindBufferMemory(mapper_.device(), readbackBuffer_, readbackMemory_, 0);

    SPDLOG_INFO("setup_framebuffer: completed successfully");
    return true;
}

void CommandDispatcher::teardown_framebuffer() {
    auto dev = mapper_.device();
    if (dev == VK_NULL_HANDLE) return;
    if (readbackBuffer_) { vkDestroyBuffer(dev, readbackBuffer_, nullptr); readbackBuffer_ = VK_NULL_HANDLE; }
    if (readbackMemory_) { vkFreeMemory(dev, readbackMemory_, nullptr); readbackMemory_ = VK_NULL_HANDLE; }
    if (mainFramebuffer_) { vkDestroyFramebuffer(dev, mainFramebuffer_, nullptr); mainFramebuffer_ = VK_NULL_HANDLE; }
    if (renderPass_) { vkDestroyRenderPass(dev, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }
    if (colorView_) { vkDestroyImageView(dev, colorView_, nullptr); colorView_ = VK_NULL_HANDLE; }
    if (colorImage_) { vkDestroyImage(dev, colorImage_, nullptr); colorImage_ = VK_NULL_HANDLE; }
    if (colorMemory_) { vkFreeMemory(dev, colorMemory_, nullptr); colorMemory_ = VK_NULL_HANDLE; }
    renderTargetImage_ = VK_NULL_HANDLE;
}

void CommandDispatcher::cache_device_procs(VkDevice dev) {
    pfnCmdPipelineBarrier2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier2"));
    pfnCmdCopyBuffer2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyBuffer2"));
    pfnCmdCopyImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyImage2"));
    pfnCmdCopyBufferToImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyBufferToImage2"));
    pfnCmdCopyImageToBuffer2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyImageToBuffer2"));
    pfnCmdBlitImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdBlitImage2"));
    pfnCmdResolveImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdResolveImage2"));
    pfnQueueSubmit2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkQueueSubmit2"));
    pfnCmdWaitEvents2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdWaitEvents2"));
    pfnCmdSetVertexInputEXT_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdSetVertexInputEXT"));
    pfnBindBufferMemory2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkBindBufferMemory2"));
    pfnBindImageMemory2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkBindImageMemory2"));
    pfnGetSemaphoreCounterValue_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkGetSemaphoreCounterValue"));
}

void CommandDispatcher::dispatch(fbs::FunctionId func_id,
                                  const uint8_t* args, size_t args_size) {
    auto it = handlers_.find(static_cast<int>(func_id));
    if (it == handlers_.end()) {
        SPDLOG_DEBUG("Host: no handler for {} (id={})",
                     fbs::EnumNameFunctionId(func_id), static_cast<int>(func_id));
        return;
    }

    VulkanDeserializer reader(args, args_size);
    try {
        it->second(*this, reader);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Handler for {} (id={}) threw: {}",
                     fbs::EnumNameFunctionId(func_id), static_cast<int>(func_id), e.what());
    }
}

bool CommandDispatcher::begin_render_pass(VkRenderPass rp, VkFramebuffer fb,
                                           uint32_t w, uint32_t h) {
    auto cb = mapper_.active_cmd();
    if (!cb) return false;
    if (inRenderPass_) end_render_pass();

    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = rp;
    bi.framebuffer = fb;
    bi.renderArea.extent = {w, h};
    bi.clearValueCount = 1;
    VkClearValue cv{};
    cv.color.float32[0] = 0.0f; cv.color.float32[1] = 0.0f;
    cv.color.float32[2] = 0.0f; cv.color.float32[3] = 1.0f;
    bi.pClearValues = &cv;

    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    inRenderPass_ = true;
    return true;
}

void CommandDispatcher::end_render_pass() {
    auto cb = mapper_.active_cmd();
    if (cb && inRenderPass_) {
        vkCmdEndRenderPass(cb);
    }
    inRenderPass_ = false;
}

bool CommandDispatcher::copy_image_to_readback() {
    auto dev = mapper_.device();
    auto q = mapper_.queue();
    if (!dev || !q) return false;

    VkImage src = (renderTargetImage_ != VK_NULL_HANDLE) ? renderTargetImage_ : colorImage_;
    if (src == VK_NULL_HANDLE || readbackBuffer_ == VK_NULL_HANDLE) return false;

    // Determine source layout and access masks based on image ownership
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkAccessFlags dstAccess = VK_ACCESS_TRANSFER_READ_BIT;

    // If this is not our own colorImage_, we don't know its exact layout.
    // Use GENERAL as a safe universal fallback to avoid invalid transitions.
    if (src != colorImage_) {
        srcLayout = VK_IMAGE_LAYOUT_GENERAL;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        srcAccess = VK_ACCESS_MEMORY_WRITE_BIT;
        dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
    }

    VkCommandBufferAllocateInfo poolAI{};
    poolAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    poolAI.commandPool = mapper_.command_pool();
    poolAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    poolAI.commandBufferCount = 1;

    VkCommandBuffer copyCmd;
    if (vkAllocateCommandBuffers(dev, &poolAI, &copyCmd) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(copyCmd, &bi);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = srcLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = src;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(copyCmd, srcStage,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {fbWidth_, fbHeight_, 1};

    vkCmdCopyImageToBuffer(copyCmd, src,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readbackBuffer_, 1, &region);

    VkImageLayout finalLayout = (src != colorImage_)
        ? VK_IMAGE_LAYOUT_GENERAL
        : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkPipelineStageFlags finalStage = (src != colorImage_)
        ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags finalAccess = (src != colorImage_)
        ? VK_ACCESS_MEMORY_READ_BIT
        : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = finalLayout;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = finalAccess;
    vkCmdPipelineBarrier(copyCmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        finalStage,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(copyCmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &copyCmd;

    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(dev, &fci, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(dev, mapper_.command_pool(), 1, &copyCmd);
        return false;
    }

    VkResult res = vkQueueSubmit(q, 1, &si, fence);
    if (res != VK_SUCCESS) {
        vkDestroyFence(dev, fence, nullptr);
        vkFreeCommandBuffers(dev, mapper_.command_pool(), 1, &copyCmd);
        return false;
    }

    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);
    vkFreeCommandBuffers(dev, mapper_.command_pool(), 1, &copyCmd);

    return true;
}

bool CommandDispatcher::flush_and_readback(std::vector<uint8_t>& out_pixels) {
    auto dev = mapper_.device();
    if (!dev) return false;

    // Wait for pending submit from vkQueueSubmit/vkQueueSubmit2
    if (hasPendingSubmit_ && pendingSubmitFence_ != VK_NULL_HANDLE) {
        vkWaitForFences(dev, 1, &pendingSubmitFence_, VK_TRUE, UINT64_MAX);
        vkDestroyFence(dev, pendingSubmitFence_, nullptr);
        pendingSubmitFence_ = VK_NULL_HANDLE;
        hasPendingSubmit_ = false;
    }

    // Copy render target pixels to readback buffer
    if (!copy_image_to_readback()) {
        SPDLOG_WARN("flush_and_readback: copy_image_to_readback failed");
        out_pixels.assign(fbWidth_ * fbHeight_ * 4, 0);
        return false;
    }

    // Read back pixels from the host-visible readback buffer
    VkDeviceSize size = fbWidth_ * fbHeight_ * 4;
    out_pixels.resize(static_cast<size_t>(size));

    if (readbackMemory_ == VK_NULL_HANDLE) {
        SPDLOG_ERROR("flush_and_readback: readbackMemory_ not allocated (framebuffer not set up)");
        return false;
    }
    void* mapped = nullptr;
    VkResult res = vkMapMemory(dev, readbackMemory_, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (res == VK_SUCCESS && mapped) {
        std::memcpy(out_pixels.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(dev, readbackMemory_);
        return true;
    }

    SPDLOG_ERROR("flush_and_readback: vkMapMemory failed: {}", static_cast<int>(res));
    return false;
}

void ResourceMapper::cleanup() {
    VkDevice dev = device_;
    if (dev == VK_NULL_HANDLE) return;

    // Destroy order: dependent resources first, then their parents

    // 1. Pipeline layouts (may reference descriptor set layouts)
    for (auto& [_, v] : pipelineLayouts_) if (v) vkDestroyPipelineLayout(dev, v, nullptr);

    // 2. Pipelines (may reference render passes, shader modules, pipeline layouts)
    for (auto& [_, v] : pipelines_) if (v) vkDestroyPipeline(dev, v, nullptr);
    for (auto& [_, v] : pipelineCaches_) if (v) vkDestroyPipelineCache(dev, v, nullptr);

    // 3. Framebuffers (reference image views, render passes)
    for (auto& [_, v] : framebuffers_) if (v) vkDestroyFramebuffer(dev, v, nullptr);

    // 4. Render passes (referenced by framebuffers and pipelines)
    for (auto& [_, v] : renderPasses_) if (v) vkDestroyRenderPass(dev, v, nullptr);

    // 5. Image views (reference images)
    for (auto& [_, v] : imageViews_) if (v) vkDestroyImageView(dev, v, nullptr);

    // 6. Images (backed by memory, must be destroyed before memory)
    for (auto& [_, v] : images_) if (v) vkDestroyImage(dev, v, nullptr);

    // 7. Buffers (backed by memory, must be destroyed before memory)
    for (auto& [_, v] : buffers_) if (v) vkDestroyBuffer(dev, v, nullptr);
    for (auto& [_, v] : samplers_) if (v) vkDestroySampler(dev, v, nullptr);
    for (auto& [_, v] : shaderModules_) if (v) vkDestroyShaderModule(dev, v, nullptr);

    // 8. Descriptor sets (must be freed before pool)
    dss_.clear();

    // 9. Descriptor pools
    for (auto& [_, v] : dps_) if (v) vkDestroyDescriptorPool(dev, v, nullptr);

    // 10. Descriptor set layouts
    for (auto& [_, v] : dsls_) if (v) vkDestroyDescriptorSetLayout(dev, v, nullptr);

    // 11. Descriptor update templates
    for (auto& [_, v] : duts_) if (v) vkDestroyDescriptorUpdateTemplate(dev, v, nullptr);

    // 12. Query pools
    for (auto& [_, v] : queryPools_) if (v) vkDestroyQueryPool(dev, v, nullptr);

    // 13. Events
    for (auto& [_, v] : events_) if (v) vkDestroyEvent(dev, v, nullptr);

    // 14. Semaphores
    for (auto& [_, v] : semaphores_) if (v) vkDestroySemaphore(dev, v, nullptr);

    // 15. Fences
    for (auto& [_, v] : fences_) if (v) vkDestroyFence(dev, v, nullptr);

    // 16. Command pools (destroys all command buffers allocated from them)
    for (auto& [_, v] : cmdPools_) if (v) vkDestroyCommandPool(dev, v, nullptr);
    cmdBufs_.clear();

    // 17. Private data slots
    for (auto& [_, v] : privateDataSlots_) if (v) vkDestroyPrivateDataSlot(dev, v, nullptr);

    // 18. Device memory (must be freed LAST, after all resources using it)
    for (auto& [_, v] : memories_) if (v) vkFreeMemory(dev, v, nullptr);
}

} // namespace omnigpu::host
