#include "command_dispatcher.h"
#include "vulkan_struct_deserializer.h"
#include "omnigpu_protocol_generated.h"
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
        r.read_handle(); // queue handle (we use our own)
        uint32_t count = r.read_u32();
        r.skip(count * sizeof(VkSubmitInfo));
        r.read_handle(); // fence
        SPDLOG_DEBUG("  vkQueueSubmit ({} submits) -> ignored (handled at submit time)", count);
    });
    REGISTER(fbs::FunctionId_vkQueuePresentKHR, [](auto& d, auto& r) {
        r.read_handle(); // queue
        r.skip(sizeof(VkPresentInfoKHR));
        SPDLOG_DEBUG("  vkQueuePresentKHR -> ignored (handled at submit time)");
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
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
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
        uint64_t pCB = r.read_handle();
        VkCommandBuffer cb;
        if (vkAllocateCommandBuffers(d.mapper_.device(), &ai, &cb) == VK_SUCCESS) {
            d.mapper_.store_command_buffer(pCB, cb);
        }
    });
    REGISTER(fbs::FunctionId_vkFreeCommandBuffers, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        uint32_t count = r.read_u32();
        auto poolH = d.mapper_.get_command_pool(pool);
        auto cbs = r.read_array<uint64_t>();
        for (auto& gcb : cbs) {
            auto cb = d.mapper_.get_command_buffer(gcb);
            if (cb && poolH) vkFreeCommandBuffers(d.mapper_.device(), poolH, 1, &cb);
        }
    });
    REGISTER(fbs::FunctionId_vkBeginCommandBuffer, [](auto& d, auto& r) {
        uint64_t cb = r.read_handle();
        VkCommandBufferBeginInfo bi{};
        r.read_raw(&bi, sizeof(bi));
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
        r.read_raw(&ai, sizeof(ai));
        ai.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pMem = r.read_handle();
        VkDeviceMemory mem;
        if (vkAllocateMemory(d.mapper_.device(), &ai, nullptr, &mem) == VK_SUCCESS) {
            d.mapper_.store_device_memory(pMem, mem);
        }
    });
    REGISTER(fbs::FunctionId_vkFreeMemory, [](auto& d, auto& r) {
        r.read_handle(); // device
        r.read_handle(); // memory
        r.skip(sizeof(VkAllocationCallbacks));
    });
    REGISTER(fbs::FunctionId_vkMapMemory, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_u64(); r.read_u64(); r.read_u32();
        r.read_handle(); // ppData (output pointer)
    });
    REGISTER(fbs::FunctionId_vkUnmapMemory, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkFlushMappedMemoryRanges, [](auto& d, auto& r) {
        r.read_handle(); auto c = r.read_u32(); r.skip(c * sizeof(VkMappedMemoryRange));
    });
    REGISTER(fbs::FunctionId_vkInvalidateMappedMemoryRanges, [](auto& d, auto& r) {
        r.read_handle(); auto c = r.read_u32(); r.skip(c * sizeof(VkMappedMemoryRange));
    });
    REGISTER(fbs::FunctionId_vkBindBufferMemory, [](auto& d, auto& r) {
        auto dev = r.read_handle();
        auto buf = r.read_handle(); auto mem = r.read_handle(); auto off = r.read_u64();
        auto b = d.mapper_.get_buffer(buf);
        auto m = d.mapper_.get_device_memory(mem);
        if (b && m) vkBindBufferMemory(d.mapper_.device(), b, m, off);
    });
    REGISTER(fbs::FunctionId_vkBindImageMemory, [](auto& d, auto& r) {
        auto dev = r.read_handle();
        auto img = r.read_handle(); auto mem = r.read_handle(); auto off = r.read_u64();
        auto i = d.mapper_.get_image(img);
        auto m = d.mapper_.get_device_memory(mem);
        if (i && m) vkBindImageMemory(d.mapper_.device(), i, m, off);
    });

    // --- Buffer ---
    REGISTER(fbs::FunctionId_vkCreateBuffer, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkBufferCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pBuf = r.read_handle();
        VkBuffer buf;
        if (vkCreateBuffer(d.mapper_.device(), &ci, nullptr, &buf) == VK_SUCCESS) {
            d.mapper_.store_buffer(pBuf, buf);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyBuffer, [](auto& d, auto& r) {
        r.read_handle(); auto buf = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto b = d.mapper_.get_buffer(buf);
        if (b) vkDestroyBuffer(d.mapper_.device(), b, nullptr);
    });
    REGISTER(fbs::FunctionId_vkCreateBufferView, [](auto& d, auto& r) {
        r.read_handle();
        auto sz = sizeof(VkBufferViewCreateInfo);
        r.skip(sz); r.skip(sizeof(VkAllocationCallbacks)); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkDestroyBufferView, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
    });

    // --- Image ---
    REGISTER(fbs::FunctionId_vkCreateImage, [](auto& d, auto& r) {
        r.read_handle();
        VkImageCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pImg = r.read_handle();
        VkImage img;
        if (vkCreateImage(d.mapper_.device(), &ci, nullptr, &img) == VK_SUCCESS) {
            d.mapper_.store_image(pImg, img);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyImage, [](auto& d, auto& r) {
        r.read_handle(); auto img = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto i = d.mapper_.get_image(img);
        if (i) vkDestroyImage(d.mapper_.device(), i, nullptr);
    });
    REGISTER(fbs::FunctionId_vkCreateImageView, [](auto& d, auto& r) {
        r.read_handle();
        VkImageViewCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pView = r.read_handle();
        // Map guest image handle to host image handle
        uint64_t gImg = reinterpret_cast<uint64_t>(ci.image);
        ci.image = d.mapper_.get_image(gImg);
        VkImageView view;
        if (vkCreateImageView(d.mapper_.device(), &ci, nullptr, &view) == VK_SUCCESS) {
            d.mapper_.store_image_view(pView, view);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyImageView, [](auto& d, auto& r) {
        r.read_handle(); auto v = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto view = d.mapper_.get_image_view(v);
        if (view) vkDestroyImageView(d.mapper_.device(), view, nullptr);
    });

    // --- Sampler ---
    REGISTER(fbs::FunctionId_vkCreateSampler, [](auto& d, auto& r) {
        r.read_handle();
        VkSamplerCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
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
    });

    // --- Pipeline Layout ---
    REGISTER(fbs::FunctionId_vkCreatePipelineLayout, [](auto& d, auto& r) {
        r.read_handle();
        VkPipelineLayoutCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPL = r.read_handle();
        VkPipelineLayout pl;
        if (vkCreatePipelineLayout(d.mapper_.device(), &ci, nullptr, &pl) == VK_SUCCESS) {
            d.mapper_.store_pipeline_layout(pPL, pl);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPipelineLayout, [](auto& d, auto& r) {
        r.read_handle(); auto pl = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline_layout(pl);
        if (p) vkDestroyPipelineLayout(d.mapper_.device(), p, nullptr);
    });

    // --- Pipelines (complex struct with pointer chains) ---
    REGISTER(fbs::FunctionId_vkCreateGraphicsPipelines, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t pCache = r.read_handle();
        VkPipelineCache cache = d.mapper_.get_pipeline_cache(pCache);
        uint32_t count = r.read_u32();
        uint64_t pPipelines = r.read_handle();

        std::vector<VkGraphicsPipelineCreateInfo> infos(count);
        for (uint32_t i = 0; i < count; i++)
            read_VkGraphicsPipelineCreateInfo(r, &infos[i]);

        r.skip(sizeof(VkAllocationCallbacks));

        std::vector<VkPipeline> pipelines(count);
        VkResult res = vkCreateGraphicsPipelines(dev, cache, count,
                                                  infos.data(), nullptr,
                                                  pipelines.data());
        static std::atomic<uint64_t> s_next_pipeline_handle{0x100000};
        if (res == VK_SUCCESS) {
            for (uint32_t i = 0; i < count; i++)
                d.mapper_.store_pipeline(s_next_pipeline_handle++, pipelines[i]);
        }
        for (auto& info : infos) free_VkGraphicsPipelineCreateInfo(&info);
        SPDLOG_DEBUG("  vkCreateGraphicsPipelines ({} pipelines) result={}", count, static_cast<int>(res));
    });
    REGISTER(fbs::FunctionId_vkCreateComputePipelines, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); uint64_t pCache = r.read_handle();
        VkPipelineCache cache = d.mapper_.get_pipeline_cache(pCache);
        uint32_t count = r.read_u32();
        uint64_t pPipelines = r.read_handle();

        std::vector<VkComputePipelineCreateInfo> infos(count);
        for (uint32_t i = 0; i < count; i++)
            read_VkComputePipelineCreateInfo(r, &infos[i]);

        r.skip(sizeof(VkAllocationCallbacks));

        std::vector<VkPipeline> pipelines(count);
        VkResult res = vkCreateComputePipelines(dev, cache, count,
                                                 infos.data(), nullptr,
                                                 pipelines.data());
        static std::atomic<uint64_t> s_next_compute_handle{0x200000};
        if (res == VK_SUCCESS) {
            for (uint32_t i = 0; i < count; i++)
                d.mapper_.store_pipeline(s_next_compute_handle++, pipelines[i]);
        }
        SPDLOG_DEBUG("  vkCreateComputePipelines ({} pipelines) result={}", count, static_cast<int>(res));
    });
    REGISTER(fbs::FunctionId_vkDestroyPipeline, [](auto& d, auto& r) {
        r.read_handle(); auto pp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline(pp);
        if (p) vkDestroyPipeline(d.mapper_.device(), p, nullptr);
    });
    REGISTER(fbs::FunctionId_vkCreatePipelineCache, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkPipelineCacheCreateInfo));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPC = r.read_handle();
        VkPipelineCache pc;
        if (vkCreatePipelineCache(d.mapper_.device(), nullptr, nullptr, &pc) == VK_SUCCESS) {
            d.mapper_.store_pipeline_cache(pPC, pc);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPipelineCache, [](auto& d, auto& r) {
        r.read_handle(); auto pc = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline_cache(pc);
        if (p) vkDestroyPipelineCache(d.mapper_.device(), p, nullptr);
    });

    // --- Render Pass ---
    REGISTER(fbs::FunctionId_vkCreateRenderPass, [](auto& d, auto& r) {
        r.read_handle();
        VkRenderPassCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pRP = r.read_handle();
        VkRenderPass rp;
        if (vkCreateRenderPass(d.mapper_.device(), &ci, nullptr, &rp) == VK_SUCCESS) {
            d.mapper_.store_render_pass(pRP, rp);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyRenderPass, [](auto& d, auto& r) {
        r.read_handle(); auto rp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto rph = d.mapper_.get_render_pass(rp);
        if (rph) vkDestroyRenderPass(d.mapper_.device(), rph, nullptr);
    });
    REGISTER(fbs::FunctionId_vkCreateRenderPass2, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkRenderPassCreateInfo2));
        r.skip(sizeof(VkAllocationCallbacks)); r.read_handle();
    });

    // --- Framebuffer ---
    REGISTER(fbs::FunctionId_vkCreateFramebuffer, [](auto& d, auto& r) {
        r.read_handle();
        VkFramebufferCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pFB = r.read_handle();
        VkFramebuffer fb;
        if (vkCreateFramebuffer(d.mapper_.device(), &ci, nullptr, &fb) == VK_SUCCESS) {
            d.mapper_.store_framebuffer(pFB, fb);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyFramebuffer, [](auto& d, auto& r) {
        r.read_handle(); auto fb = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto f = d.mapper_.get_framebuffer(fb);
        if (f) vkDestroyFramebuffer(d.mapper_.device(), f, nullptr);
    });

    // --- Descriptor Set Layout ---
    REGISTER(fbs::FunctionId_vkCreateDescriptorSetLayout, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorSetLayoutCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pDSL = r.read_handle();
        VkDescriptorSetLayout dsl;
        if (vkCreateDescriptorSetLayout(d.mapper_.device(), &ci, nullptr, &dsl) == VK_SUCCESS) {
            d.mapper_.store_descriptor_set_layout(pDSL, dsl);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorSetLayout, [](auto& d, auto& r) {
        r.read_handle(); auto g = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto h = d.mapper_.get_dsl(g);
        if (h) vkDestroyDescriptorSetLayout(d.mapper_.device(), h, nullptr);
    });
    REGISTER(fbs::FunctionId_vkCreateDescriptorPool, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorPoolCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pDP = r.read_handle();
        VkDescriptorPool dp;
        if (vkCreateDescriptorPool(d.mapper_.device(), &ci, nullptr, &dp) == VK_SUCCESS) {
            d.mapper_.store_descriptor_pool(pDP, dp);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorPool, [](auto& d, auto& r) {
        r.read_handle(); auto dp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_dp(dp);
        if (p) vkDestroyDescriptorPool(d.mapper_.device(), p, nullptr);
    });
    REGISTER(fbs::FunctionId_vkAllocateDescriptorSets, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorSetAllocateInfo ai{};
        r.read_raw(&ai, sizeof(ai));
        ai.pNext = nullptr;
        uint64_t pDS = r.read_handle();
        VkDescriptorSet ds;
        if (vkAllocateDescriptorSets(d.mapper_.device(), &ai, &ds) == VK_SUCCESS) {
            d.mapper_.store_descriptor_set(pDS, ds);
        }
    });
    REGISTER(fbs::FunctionId_vkFreeDescriptorSets, [](auto& d, auto& r) {
        r.read_handle(); auto dp = r.read_handle(); uint32_t c = r.read_u32();
        auto v = r.read_array<uint64_t>();
        auto pool = d.mapper_.get_dp(dp);
        for (auto& gds : v) {
            auto ds = d.mapper_.get_ds(gds);
            if (ds && pool) vkFreeDescriptorSets(d.mapper_.device(), pool, 1, &ds);
        }
    });
    REGISTER(fbs::FunctionId_vkUpdateDescriptorSets, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t wc = r.read_u32();
        std::vector<VkWriteDescriptorSet> writes(wc);
        std::vector<VkDescriptorImageInfo*> imgPtrs(wc, nullptr);
        std::vector<VkDescriptorBufferInfo*> bufPtrs(wc, nullptr);
        std::vector<VkBufferView*> viewPtrs(wc, nullptr);
        for (uint32_t i = 0; i < wc; i++)
            read_VkWriteDescriptorSet(r, &writes[i], &imgPtrs[i], &bufPtrs[i], &viewPtrs[i]);
        uint32_t cc = r.read_u32();
        std::vector<VkCopyDescriptorSet> copies(cc);
        for (uint32_t i = 0; i < cc; i++)
            r.read_raw(&copies[i], sizeof(VkCopyDescriptorSet));

        // Remap handles
        for (auto& w : writes) {
            w.dstSet = d.mapper_.get_ds(reinterpret_cast<uint64_t>(w.dstSet));
            if (w.pImageInfo) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* img = const_cast<VkDescriptorImageInfo*>(&w.pImageInfo[j]);
                    img->sampler = d.mapper_.get_sampler(reinterpret_cast<uint64_t>(img->sampler));
                    img->imageView = d.mapper_.get_image_view(reinterpret_cast<uint64_t>(img->imageView));
                }
            }
            if (w.pBufferInfo) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* buf = const_cast<VkDescriptorBufferInfo*>(&w.pBufferInfo[j]);
                    buf->buffer = d.mapper_.get_buffer(reinterpret_cast<uint64_t>(buf->buffer));
                }
            }
        }
        vkUpdateDescriptorSets(d.mapper_.device(), wc, writes.data(), cc, copies.data());
        SPDLOG_DEBUG("  vkUpdateDescriptorSets ({} writes, {} copies)", wc, cc);
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
    });
    REGISTER(fbs::FunctionId_vkWaitForFences, [](auto& d, auto& r) {
        auto dev = r.read_handle();
        uint32_t count = r.read_u32();
        r.skip(count * sizeof(uint64_t)); // fence handles
        VkBool32 waitAll = r.read_bool();
        uint64_t timeout = r.read_u64();
        vkWaitForFences(d.mapper_.device(), 0, nullptr, waitAll, timeout);
    });
    REGISTER(fbs::FunctionId_vkResetFences, [](auto& d, auto& r) {
        r.read_handle(); uint32_t c = r.read_u32();
        r.skip(c * sizeof(uint64_t));
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
        uint32_t first = r.read_u32(); uint32_t count = r.read_u32();
        auto buf_handles = r.read_array<uint64_t>();
        auto offsets = r.read_array<uint64_t>();
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
        uint32_t firstSet = r.read_u32(); uint32_t descCount = r.read_u32();
        auto desc_handles = r.read_array<uint64_t>();
        uint32_t dynCount = r.read_u32();
        auto dynOffsets = r.read_array<uint32_t>();
        if (cb) {
            VkPipelineLayout pl = d.mapper_.get_pipeline_layout(layout);
            std::vector<VkDescriptorSet> dss;
            for (auto& g : desc_handles) dss.push_back(d.mapper_.get_ds(g));
            vkCmdBindDescriptorSets(cb, bp, pl, firstSet,
                                    static_cast<uint32_t>(dss.size()),
                                    dss.data(), dynCount, dynOffsets.data());
        }
    });
    REGISTER(fbs::FunctionId_vkCmdSetViewport, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32(); uint32_t count = r.read_u32();
        auto vps = r.read_array<VkViewport>();
        if (cb) vkCmdSetViewport(cb, first, static_cast<uint32_t>(vps.size()), vps.data());
    });
    REGISTER(fbs::FunctionId_vkCmdSetScissor, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32(); uint32_t count = r.read_u32();
        auto scis = r.read_array<VkRect2D>();
        if (cb) vkCmdSetScissor(cb, first, static_cast<uint32_t>(scis.size()), scis.data());
    });
    REGISTER(fbs::FunctionId_vkCmdPushConstants, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t layout = r.read_handle();
        VkShaderStageFlags stages = r.read_u32();
        uint32_t offset = r.read_u32(); uint32_t size = r.read_u32();
        auto data = r.read_array<uint8_t>();
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
        uint32_t rc = r.read_u32();
        auto regions = r.read_array<VkBufferCopy>();
        auto s = d.mapper_.get_buffer(src); auto d2 = d.mapper_.get_buffer(dst);
        if (cb && s && d2)
            vkCmdCopyBuffer(cb, s, d2, static_cast<uint32_t>(regions.size()), regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); r.read_u32(); // srcLayout
        uint64_t dst = r.read_handle(); r.read_u32(); // dstLayout
        uint32_t rc = r.read_u32(); auto regions = r.read_array<VkImageCopy>();
        auto s = d.mapper_.get_image(src); auto d2 = d.mapper_.get_image(dst);
        if (cb && s && d2) vkCmdCopyImage(cb, s, VK_IMAGE_LAYOUT_GENERAL, d2, VK_IMAGE_LAYOUT_GENERAL,
                                           static_cast<uint32_t>(regions.size()), regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdBlitImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dst = r.read_handle(); VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t rc = r.read_u32();
        auto regions = r.read_array<VkImageBlit>();
        VkFilter filter = static_cast<VkFilter>(r.read_u32());
        auto s = d.mapper_.get_image(src);
        auto d2 = d.mapper_.get_image(dst);
        if (cb && s && d2)
            vkCmdBlitImage(cb, s, srcLayout, d2, dstLayout,
                           static_cast<uint32_t>(regions.size()), regions.data(), filter);
    });
    REGISTER(fbs::FunctionId_vkCmdCopyBufferToImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_handle(); r.read_handle(); r.read_u32();
        uint32_t rc = r.read_u32(); r.skip(rc * sizeof(VkBufferImageCopy));
        if (cb) SPDLOG_DEBUG("  vkCmdCopyBufferToImage -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImageToBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_handle(); r.read_u32(); r.read_handle();
        uint32_t rc = r.read_u32(); r.skip(rc * sizeof(VkBufferImageCopy));
        if (cb) SPDLOG_DEBUG("  vkCmdCopyImageToBuffer -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdUpdateBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t dst = r.read_handle();
        uint64_t off = r.read_u64(); uint64_t sz = r.read_u64();
        auto data = r.read_array<uint8_t>();
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
        uint32_t rc = r.read_u32();
        auto ranges = r.read_array<VkImageSubresourceRange>();
        auto i = d.mapper_.get_image(img);
        if (cb && i)
            vkCmdClearColorImage(cb, i, layout, &color,
                                 static_cast<uint32_t>(ranges.size()), ranges.data());
    });
    REGISTER(fbs::FunctionId_vkCmdClearDepthStencilImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_handle(); r.read_u32(); r.skip(sizeof(VkClearDepthStencilValue));
        uint32_t rc = r.read_u32(); r.skip(rc * sizeof(VkImageSubresourceRange));
        if (cb) SPDLOG_DEBUG("  vkCmdClearDepthStencilImage -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdClearAttachments, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ac = r.read_u32(); r.skip(ac * sizeof(VkClearAttachment));
        uint32_t rc = r.read_u32(); r.skip(rc * sizeof(VkClearRect));
        if (cb) SPDLOG_DEBUG("  vkCmdClearAttachments -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdResolveImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_handle(); r.read_u32(); r.read_handle(); r.read_u32();
        uint32_t rc = r.read_u32(); r.skip(rc * sizeof(VkImageResolve));
        if (cb) SPDLOG_DEBUG("  vkCmdResolveImage -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdExecuteCommands, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t c = r.read_u32(); r.skip(c * sizeof(uint64_t));
        if (cb) SPDLOG_DEBUG("  vkCmdExecuteCommands ({} secondary CmdBuffers) -> STUB", c);
    });

    // --- Barrier ---
    REGISTER(fbs::FunctionId_vkCmdPipelineBarrier, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineStageFlags src = r.read_u32(); VkPipelineStageFlags dst = r.read_u32();
        VkDependencyFlags dep = r.read_u32();
        uint32_t mc = r.read_u32(); r.skip(mc * sizeof(VkMemoryBarrier));
        uint32_t bc = r.read_u32(); r.skip(bc * sizeof(VkBufferMemoryBarrier));
        uint32_t ic = r.read_u32(); r.skip(ic * sizeof(VkImageMemoryBarrier));
        if (cb) vkCmdPipelineBarrier(cb, src, dst, dep, 0, nullptr, 0, nullptr, 0, nullptr);
    });

    // --- Render Pass ---
    REGISTER(fbs::FunctionId_vkCmdBeginRenderPass, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkRenderPassBeginInfo bi{};
        r.read_raw(&bi, sizeof(bi));
        bi.pNext = nullptr;
        r.read_u32(); // contents
        if (cb) {
            // Map guest render pass and framebuffer to host handles
            bi.renderPass = d.mapper_.get_render_pass(reinterpret_cast<uint64_t>(bi.renderPass));
            bi.framebuffer = d.mapper_.get_framebuffer(reinterpret_cast<uint64_t>(bi.framebuffer));
            vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        }
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
        uint32_t ec = r.read_u32(); r.skip(ec * sizeof(uint64_t));
        r.read_u32(); r.read_u32(); // src/dst stage masks
        uint32_t mc = r.read_u32(); r.skip(mc * sizeof(VkMemoryBarrier));
        uint32_t bc = r.read_u32(); r.skip(bc * sizeof(VkBufferMemoryBarrier));
        uint32_t ic = r.read_u32(); r.skip(ic * sizeof(VkImageMemoryBarrier));
        if (cb) vkCmdWaitEvents(cb, 0, nullptr, 0, 0, 0, nullptr, 0, nullptr, 0, nullptr);
    });

    // --- Query commands ---
    REGISTER(fbs::FunctionId_vkCmdBeginQuery, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32(); r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdBeginQuery(cb, qph, q, 0);
    });
    REGISTER(fbs::FunctionId_vkCmdEndQuery, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdEndQuery(cb, qph, q);
    });
    REGISTER(fbs::FunctionId_vkCmdWriteTimestamp, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_u32(); uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, qph, q);
    });
    REGISTER(fbs::FunctionId_vkCmdResetQueryPool, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t fq = r.read_u32(); uint32_t qc = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdResetQueryPool(cb, qph, fq, qc);
    });
    REGISTER(fbs::FunctionId_vkCmdCopyQueryPoolResults, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_handle(); r.read_u32(); r.read_u32(); r.read_handle();
        r.read_u64(); r.read_u64(); r.read_u32();
        if (cb) SPDLOG_DEBUG("  vkCmdCopyQueryPoolResults -> STUB");
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
        auto vps = r.read_array<VkViewport>();
        if (cb) vkCmdSetViewportWithCount(cb, count, vps.data());
    });
    REGISTER(fbs::FunctionId_vkCmdSetScissorWithCount, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t count = r.read_u32();
        auto scis = r.read_array<VkRect2D>();
        if (cb) vkCmdSetScissorWithCount(cb, count, scis.data());
    });

    // --- Synchronization2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdPipelineBarrier2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        // Read VkDependencyInfo counts, then skip individual barriers
        uint32_t dep_flags = r.read_u32();
        uint32_t mem_br = r.read_u32(); r.skip(mem_br * sizeof(VkMemoryBarrier2));
        uint32_t buf_br = r.read_u32(); r.skip(buf_br * sizeof(VkBufferMemoryBarrier2));
        uint32_t img_br = r.read_u32(); r.skip(img_br * sizeof(VkImageMemoryBarrier2));
        // Call sync2 barrier (empty dependency info is valid)
        VkDependencyInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        if (cb) vkCmdPipelineBarrier2(cb, &di);
    });
    REGISTER(fbs::FunctionId_vkCmdBeginRendering, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkRenderingInfo ri{};
        read_VkRenderingInfo(r, &ri);
        if (cb) {
            // Remap image view handles in attachments
            for (uint32_t i = 0; i < ri.colorAttachmentCount && ri.pColorAttachments; i++) {
                auto& att = const_cast<VkRenderingAttachmentInfo&>(ri.pColorAttachments[i]);
                att.imageView = d.mapper_.get_image_view(reinterpret_cast<uint64_t>(att.imageView));
                att.resolveImageView = d.mapper_.get_image_view(reinterpret_cast<uint64_t>(att.resolveImageView));
            }
            vkCmdBeginRendering(cb, &ri);
        }
    });
    REGISTER(fbs::FunctionId_vkCmdEndRendering, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        if (cb) vkCmdEndRendering(cb);
    });

    // --- Copy commands 2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdCopyBuffer2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkCopyBufferInfo2));
        if (cb) SPDLOG_DEBUG("  vkCmdCopyBuffer2 -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkCopyImageInfo2));
        if (cb) SPDLOG_DEBUG("  vkCmdCopyImage2 -> STUB");
    });

    // --- QueueSubmit2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkQueueSubmit2, [](auto& d, auto& r) {
        r.read_handle(); uint32_t c = r.read_u32();
        r.skip(c * sizeof(VkSubmitInfo2)); r.read_handle();
        SPDLOG_DEBUG("  vkQueueSubmit2 ({} submits) -> STUB", c);
    });

    // --- Private Data (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCreatePrivateDataSlot, [](auto& d, auto& r) {
        r.read_handle();
        VkPrivateDataSlotCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSlot = r.read_handle();
        VkPrivateDataSlot slot;
        if (vkCreatePrivateDataSlot(d.mapper_.device(), &ci, nullptr, &slot) == VK_SUCCESS) {
            d.mapper_.store_private_data_slot(pSlot, slot);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPrivateDataSlot, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        SPDLOG_DEBUG("  vkDestroyPrivateDataSlot -> STUB");
    });
    REGISTER(fbs::FunctionId_vkSetPrivateData, [](auto& d, auto& r) {
        r.read_handle(); r.read_u32(); r.read_u64(); r.read_handle(); r.read_u64();
    });

    // --- Swapchain (KHR) ---
    REGISTER(fbs::FunctionId_vkCreateSwapchainKHR, [](auto& d, auto& r) {
        r.read_handle();
        VkSwapchainCreateInfoKHR ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSC = r.read_handle();
        VkSwapchainKHR sc;
        if (vkCreateSwapchainKHR(d.mapper_.device(), &ci, nullptr, &sc) == VK_SUCCESS) {
        }
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
    });
    REGISTER(fbs::FunctionId_vkUpdateDescriptorSetWithTemplate, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t gDS = r.read_handle();
        uint64_t gTpl = r.read_handle();
        auto ds = d.mapper_.get_ds(gDS);
        auto tpl = d.mapper_.get_descriptor_update_template(gTpl);
        auto data = r.read_array<uint8_t>();
        if (dev && ds && tpl && data.size() > 0) {
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
        uint32_t bc = r.read_u32(); r.skip(bc * sizeof(VkVertexInputBindingDescription2EXT));
        uint32_t ac = r.read_u32(); r.skip(ac * sizeof(VkVertexInputAttributeDescription2EXT));
        if (cb) SPDLOG_DEBUG("  vkCmdSetVertexInputEXT -> STUB (needs GetDeviceProcAddr)");
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
        r.read_handle(); uint32_t c = r.read_u32(); r.skip(c * sizeof(VkBindBufferMemoryInfo));
    });
    REGISTER(fbs::FunctionId_vkBindImageMemory2, [](auto& d, auto& r) {
        r.read_handle(); uint32_t c = r.read_u32(); r.skip(c * sizeof(VkBindImageMemoryInfo));
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
        r.read_handle(); r.read_u64(); r.read_handle(); r.read_u64();
        r.read_u32(); r.read_u32();
        if (cb) SPDLOG_DEBUG("  vkCmdDrawIndirectCount -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdDrawIndexedIndirectCount, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_handle(); r.read_u64(); r.read_handle(); r.read_u64();
        r.read_u32(); r.read_u32();
        if (cb) SPDLOG_DEBUG("  vkCmdDrawIndexedIndirectCount -> STUB");
    });

    // --- Render pass 2 (1.2) ---
    REGISTER(fbs::FunctionId_vkCmdBeginRenderPass2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkRenderPassBeginInfo) + sizeof(VkSubpassBeginInfo));
        if (cb) SPDLOG_DEBUG("  vkCmdBeginRenderPass2 -> STUB (use vkCmdBeginRenderPass)");
    });
    REGISTER(fbs::FunctionId_vkCmdEndRenderPass2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkSubpassEndInfo));
        if (cb) SPDLOG_DEBUG("  vkCmdEndRenderPass2 -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdNextSubpass2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkSubpassBeginInfo) + sizeof(VkSubpassEndInfo));
        if (cb) SPDLOG_DEBUG("  vkCmdNextSubpass2 -> STUB");
    });

    // --- Wait/Signal semaphore (1.2) ---
    REGISTER(fbs::FunctionId_vkWaitSemaphores, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkSemaphoreWaitInfo)); r.read_u64();
    });
    REGISTER(fbs::FunctionId_vkSignalSemaphore, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkSemaphoreSignalInfo));
    });
    REGISTER(fbs::FunctionId_vkGetSemaphoreCounterValue, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_handle();
    });

    // --- Reset query pool (1.2) ---
    REGISTER(fbs::FunctionId_vkResetQueryPool, [](auto& d, auto& r) {
        r.read_handle(); auto qp = r.read_handle(); r.read_u32(); r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (qph) vkResetQueryPool(d.mapper_.device(), qph, 0, 0);
    });

    // --- Bind vertex buffers 2 (1.3) ---
    REGISTER(fbs::FunctionId_vkCmdBindVertexBuffers2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t first = r.read_u32(); uint32_t count = r.read_u32();
        auto bufs = r.read_array<uint64_t>();
        auto offs = r.read_array<uint64_t>();
        auto sizes = r.read_array<uint64_t>();
        auto strides = r.read_array<uint64_t>();
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
        uint64_t evt = r.read_handle(); r.skip(sizeof(VkDependencyInfo));
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdSetEvent2(cb, e, nullptr);
    });
    REGISTER(fbs::FunctionId_vkCmdWaitEvents2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ec = r.read_u32(); r.skip(ec * sizeof(uint64_t));
        r.skip(ec * sizeof(VkDependencyInfo));
        if (cb) SPDLOG_DEBUG("  vkCmdWaitEvents2 -> STUB");
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
        r.skip(sizeof(VkBlitImageInfo2));
        if (cb) SPDLOG_DEBUG("  vkCmdBlitImage2 -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdResolveImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkResolveImageInfo2));
        if (cb) SPDLOG_DEBUG("  vkCmdResolveImage2 -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdCopyBufferToImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkCopyBufferToImageInfo2));
        if (cb) SPDLOG_DEBUG("  vkCmdCopyBufferToImage2 -> STUB");
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImageToBuffer2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.skip(sizeof(VkCopyImageToBufferInfo2));
        if (cb) SPDLOG_DEBUG("  vkCmdCopyImageToBuffer2 -> STUB");
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

void CommandDispatcher::set_device(VkPhysicalDevice physDev, VkDevice device,
                                    VkQueue queue, uint32_t queueFamily,
                                    VkCommandPool cmdPool) {
    mapper_.set_device(device, queue, queueFamily, cmdPool);
    physDev_ = physDev;
}

void CommandDispatcher::set_framebuffer_size(uint32_t w, uint32_t h) {
    fbWidth_ = w;
    fbHeight_ = h;
}

bool CommandDispatcher::setup_framebuffer() {
    // Create a color image + view + framebuffer for the main swapchain
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

    if (vkCreateImage(mapper_.device(), &imgInfo, nullptr, &colorImage_) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(mapper_.device(), colorImage_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    // Find device-local memory type
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev_, &memProps);
    allocInfo.memoryTypeIndex = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }

    if (vkAllocateMemory(mapper_.device(), &allocInfo, nullptr, &colorMemory_) != VK_SUCCESS)
        return false;
    vkBindImageMemory(mapper_.device(), colorImage_, colorMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(mapper_.device(), &viewInfo, nullptr, &colorView_) != VK_SUCCESS)
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
    if (vkCreateRenderPass(mapper_.device(), &rpInfo, nullptr, &renderPass_) != VK_SUCCESS)
        return false;

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &colorView_;
    fbInfo.width = fbWidth_;
    fbInfo.height = fbHeight_;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(mapper_.device(), &fbInfo, nullptr, &mainFramebuffer_) != VK_SUCCESS)
        return false;

    // Readback buffer
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = fbWidth_ * fbHeight_ * 4;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(mapper_.device(), &bufInfo, nullptr, &readbackBuffer_) != VK_SUCCESS)
        return false;

    VkMemoryRequirements rbMemReq;
    vkGetBufferMemoryRequirements(mapper_.device(), readbackBuffer_, &rbMemReq);

    VkMemoryAllocateInfo rbAlloc{};
    rbAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    rbAlloc.allocationSize = rbMemReq.size;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            rbAlloc.memoryTypeIndex = i;
            break;
        }
    }
    if (vkAllocateMemory(mapper_.device(), &rbAlloc, nullptr, &readbackMemory_) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(mapper_.device(), readbackBuffer_, readbackMemory_, 0);

    return true;
}

void CommandDispatcher::teardown_framebuffer() {
    auto dev = mapper_.device();
    if (dev == VK_NULL_HANDLE) return;
    if (readbackBuffer_) vkDestroyBuffer(dev, readbackBuffer_, nullptr);
    if (readbackMemory_) vkFreeMemory(dev, readbackMemory_, nullptr);
    if (mainFramebuffer_) vkDestroyFramebuffer(dev, mainFramebuffer_, nullptr);
    if (renderPass_) vkDestroyRenderPass(dev, renderPass_, nullptr);
    if (colorView_) vkDestroyImageView(dev, colorView_, nullptr);
    if (colorImage_) vkDestroyImage(dev, colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(dev, colorMemory_, nullptr);
}

void CommandDispatcher::dispatch(fbs::FunctionId func_id,
                                  const uint8_t* args, size_t args_size) {
    auto it = handlers_.find(static_cast<int>(func_id));
    if (it == handlers_.end()) {
        SPDLOG_DEBUG("Host: no handler for func_id={}", static_cast<int>(func_id));
        return;
    }

    VulkanDeserializer reader(args, args_size);
    try {
        it->second(*this, reader);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Handler for func_id={} threw: {}", static_cast<int>(func_id), e.what());
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
    VkClearColorValue clear = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkClearValue cv;
    cv.color = clear;
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

bool CommandDispatcher::flush_and_readback(std::vector<uint8_t>& out_pixels) {
    auto dev = mapper_.device();
    auto q = mapper_.queue();
    auto cb = mapper_.active_cmd();
    if (!dev || !q || !cb) return false;

    end_render_pass();

    VkResult res = vkEndCommandBuffer(cb);
    if (res != VK_SUCCESS) {
        SPDLOG_ERROR("flush: vkEndCommandBuffer failed: {}", static_cast<int>(res));
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(dev, &fci, nullptr, &fence) != VK_SUCCESS) {
        SPDLOG_ERROR("flush: vkCreateFence failed");
        return false;
    }

    res = vkQueueSubmit(q, 1, &si, fence);
    if (res != VK_SUCCESS) {
        vkDestroyFence(dev, fence, nullptr);
        return false;
    }

    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);

    // Read back pixels
    VkDeviceSize size = fbWidth_ * fbHeight_ * 4;
    out_pixels.resize(static_cast<size_t>(size));

    void* mapped = nullptr;
    res = vkMapMemory(dev, readbackMemory_, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (res == VK_SUCCESS && mapped) {
        std::memcpy(out_pixels.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(dev, readbackMemory_);
        return true;
    }
    return false;
}

void ResourceMapper::cleanup() {
    VkDevice dev = device_;
    if (dev == VK_NULL_HANDLE) return;
    for (auto& [_, v] : buffers_) if (v) vkDestroyBuffer(dev, v, nullptr);
    for (auto& [_, v] : images_) if (v) vkDestroyImage(dev, v, nullptr);
    for (auto& [_, v] : imageViews_) if (v) vkDestroyImageView(dev, v, nullptr);
    for (auto& [_, v] : samplers_) if (v) vkDestroySampler(dev, v, nullptr);
    for (auto& [_, v] : shaderModules_) if (v) vkDestroyShaderModule(dev, v, nullptr);
    for (auto& [_, v] : pipelineLayouts_) if (v) vkDestroyPipelineLayout(dev, v, nullptr);
    for (auto& [_, v] : pipelines_) if (v) vkDestroyPipeline(dev, v, nullptr);
    for (auto& [_, v] : pipelineCaches_) if (v) vkDestroyPipelineCache(dev, v, nullptr);
    for (auto& [_, v] : renderPasses_) if (v) vkDestroyRenderPass(dev, v, nullptr);
    for (auto& [_, v] : framebuffers_) if (v) vkDestroyFramebuffer(dev, v, nullptr);
    for (auto& [_, v] : dsls_) if (v) vkDestroyDescriptorSetLayout(dev, v, nullptr);
    for (auto& [_, v] : dps_) if (v) vkDestroyDescriptorPool(dev, v, nullptr);
    for (auto& [_, v] : fences_) if (v) vkDestroyFence(dev, v, nullptr);
    for (auto& [_, v] : semaphores_) if (v) vkDestroySemaphore(dev, v, nullptr);
    for (auto& [_, v] : events_) if (v) vkDestroyEvent(dev, v, nullptr);
    for (auto& [_, v] : queryPools_) if (v) vkDestroyQueryPool(dev, v, nullptr);
    for (auto& [_, v] : memories_) if (v) vkFreeMemory(dev, v, nullptr);
    for (auto& [_, v] : cmdPools_) if (v) vkDestroyCommandPool(dev, v, nullptr);
    for (auto& [_, v] : cmdBufs_) {}  // freed with pool
    for (auto& [_, v] : privateDataSlots_) if (v) vkDestroyPrivateDataSlot(dev, v, nullptr);
}

} // namespace omnigpu::host
