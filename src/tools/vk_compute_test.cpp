// OmniGPU Compute Test — tests real GPU arithmetic via remote ICD
// Tests: vector addition, matrix multiplication, readback verification

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <cmath>

#include "compute_add_spv.h"
#include "compute_mul_spv.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#define CHK(r, msg) do { \
    VkResult _r = (r); \
    if (_r != VK_SUCCESS) { printf("FAIL [%s:%d]: %s -> %d\n", __FILE__, __LINE__, msg, _r); exit(1); } \
} while(0)

#define CHK_PTR(p, msg) do { \
    if (!(p)) { printf("FAIL [%s:%d]: %s -> NULL\n", __FILE__, __LINE__, msg); exit(1); } \
} while(0)

static void wait_on_exit() {
    printf("\nPress Enter to exit...");
    fflush(stdout);
    getchar();
}

// -----------------------------------------------------------------------
// Find a queue family with COMPUTE bit
// -----------------------------------------------------------------------
static int find_compute_qf(VkPhysicalDevice pd) {
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, props.data());
    for (uint32_t i = 0; i < n; i++)
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) return static_cast<int>(i);
    return -1;
}

// -----------------------------------------------------------------------
// Create a buffer with given size and usage
// -----------------------------------------------------------------------
static VkBuffer make_buffer(VkPhysicalDevice physDev, VkDevice dev, VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags memProps,
                            VkDeviceMemory* pMemory, uint32_t memTypeIndex) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buf;
    CHK(vkCreateBuffer(dev, &bci, nullptr, &buf), "Create buffer");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    if (memTypeIndex == UINT32_MAX) {
        VkPhysicalDeviceMemoryProperties pmp;
        vkGetPhysicalDeviceMemoryProperties(physDev, &pmp);
        for (uint32_t i = 0; i < pmp.memoryTypeCount; i++) {
            if ((mr.memoryTypeBits & (1 << i)) &&
                (pmp.memoryTypes[i].propertyFlags & memProps) == memProps) {
                memTypeIndex = i;
                break;
            }
        }
    }
    mai.memoryTypeIndex = memTypeIndex;

    CHK(vkAllocateMemory(dev, &mai, nullptr, pMemory), "Alloc memory");
    CHK(vkBindBufferMemory(dev, buf, *pMemory, 0), "Bind memory");
    return buf;
}

// -----------------------------------------------------------------------
// Upload data to a host-visible buffer
// -----------------------------------------------------------------------
static void upload(VkDevice dev, VkDeviceMemory mem, const void* data, VkDeviceSize size) {
    void* mapped;
    CHK(vkMapMemory(dev, mem, 0, size, 0, &mapped), "Map memory");
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(dev, mem);
}

// -----------------------------------------------------------------------
// Download data from a host-visible buffer
// -----------------------------------------------------------------------
static void download(VkDevice dev, VkDeviceMemory mem, void* data, VkDeviceSize size) {
    void* mapped;
    CHK(vkMapMemory(dev, mem, 0, size, 0, &mapped), "Map memory");
    std::memcpy(data, mapped, static_cast<size_t>(size));
    vkUnmapMemory(dev, mem);
}

// -----------------------------------------------------------------------
// Test 1: Vector Addition
// -----------------------------------------------------------------------
static void test_vector_add(VkPhysicalDevice physDev, VkDevice device, VkQueue queue, uint32_t qf) {
    printf("\n--- [Test 1] Vector Addition ---\n");
    const uint32_t N = 1024; // elements, must be multiple of workgroup size (256)
    const VkDeviceSize bufSize = N * sizeof(float);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

    // Generate input data
    std::vector<float> inA(N), inB(N), expected(N);
    for (uint32_t i = 0; i < N; i++) {
        inA[i] = dist(rng);
        inB[i] = dist(rng);
        expected[i] = inA[i] + inB[i];
    }

    // Create compute pipeline
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = compute_add_spv_size;
    smci.pCode = compute_add_spv;

    VkShaderModule sm;
    CHK(vkCreateShaderModule(device, &smci, nullptr, &sm), "Create shader module");

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = sm;
    stage.pName = "main";

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 0;
    plci.pPushConstantRanges = nullptr;
    plci.setLayoutCount = 0;
    plci.pSetLayouts = nullptr;

    VkPipelineLayout layout;
    CHK(vkCreatePipelineLayout(device, &plci, nullptr, &layout), "Create pipeline layout");

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = layout;

    VkPipeline pipeline;
    CHK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
        "Create compute pipeline");

    // Create buffers
    VkDeviceMemory memA, memB, memOut;
    VkBuffer bufA = make_buffer(physDev, device, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &memA, UINT32_MAX);
    VkBuffer bufB = make_buffer(physDev, device, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &memB, UINT32_MAX);
    VkBuffer bufOut = make_buffer(physDev, device, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &memOut, UINT32_MAX);

    // Upload input data
    upload(device, memA, inA.data(), bufSize);
    upload(device, memB, inB.data(), bufSize);

    // Create descriptor set
    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1] = bindings[0]; bindings[1].binding = 1;
    bindings[2] = bindings[0]; bindings[2].binding = 2;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 3;
    dslci.pBindings = bindings;

    VkDescriptorSetLayout dsl;
    CHK(vkCreateDescriptorSetLayout(device, &dslci, nullptr, &dsl), "Create DSL");

    // Update pipeline layout with descriptor set
    VkPipelineLayoutCreateInfo plci2{};
    plci2.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci2.setLayoutCount = 1;
    plci2.pSetLayouts = &dsl;

    VkPipelineLayout layout2;
    CHK(vkCreatePipelineLayout(device, &plci2, nullptr, &layout2), "Create pipeline layout 2");

    VkComputePipelineCreateInfo cpci2{};
    cpci2.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci2.stage = stage;
    cpci2.layout = layout2;

    VkPipeline pipeline2;
    CHK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci2, nullptr, &pipeline2),
        "Create compute pipeline 2");
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);

    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps.descriptorCount = 3;

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;

    VkDescriptorPool pool;
    CHK(vkCreateDescriptorPool(device, &dpci, nullptr, &pool), "Create descriptor pool");

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;

    VkDescriptorSet ds;
    CHK(vkAllocateDescriptorSets(device, &dsai, &ds), "Allocate descriptor set");

    VkDescriptorBufferInfo dbi[3] = {};
    dbi[0].buffer = bufA; dbi[0].range = VK_WHOLE_SIZE;
    dbi[1].buffer = bufB; dbi[1].range = VK_WHOLE_SIZE;
    dbi[2].buffer = bufOut; dbi[2].range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet wds[3] = {};
    for (int i = 0; i < 3; i++) {
        wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[i].dstSet = ds;
        wds[i].dstBinding = i;
        wds[i].descriptorCount = 1;
        wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(device, 3, wds, 0, nullptr);

    // Allocate command buffer
    VkCommandPoolCreateInfo cpci_cmd{};
    cpci_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci_cmd.queueFamilyIndex = qf;
    cpci_cmd.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool cmdPool;
    CHK(vkCreateCommandPool(device, &cpci_cmd, nullptr, &cmdPool), "Create command pool");

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    CHK(vkAllocateCommandBuffers(device, &cbai, &cmd), "Allocate command buffer");

    // Record + submit compute work
    for (int warmup = 0; warmup < 3; warmup++) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHK(vkBeginCommandBuffer(cmd, &bi), "Begin command buffer");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline2);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                layout2, 0, 1, &ds, 0, nullptr);
        vkCmdDispatch(cmd, N / 256, 1, 1);

        CHK(vkEndCommandBuffer(cmd), "End command buffer");

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        CHK(vkCreateFence(device, &fci, nullptr, &fence), "Create fence");
        CHK(vkQueueSubmit(queue, 1, &si, fence), "Queue submit");
        CHK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Wait fence");
        vkDestroyFence(device, fence, nullptr);
        vkResetCommandPool(device, cmdPool, 0);
    }

    // Timed run
    auto t0 = std::chrono::high_resolution_clock::now();
    const int ITERS = 100;
    for (int iter = 0; iter < ITERS; iter++) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline2);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout2, 0, 1, &ds, 0, nullptr);
        vkCmdDispatch(cmd, N / 256, 1, 1);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFence fence;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device, &fci, nullptr, &fence);
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
        vkResetCommandPool(device, cmdPool, 0);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d dispatches in %.2f ms = %.2f us/dispatch\n",
           ITERS, ms_total, ms_total * 1000.0 / ITERS);

    // Read back
    std::vector<float> result(N);
    download(device, memOut, result.data(), bufSize);

    // Verify
    uint32_t errors = 0;
    for (uint32_t i = 0; i < N; i++) {
        float diff = std::fabs(result[i] - expected[i]);
        float maxVal = (std::max)(1.0f, std::fabs(expected[i]));
        if (diff / maxVal > 0.001f) {
            if (errors < 5)
                printf("  MISMATCH [%u]: expected %.4f, got %.4f\n", i, expected[i], result[i]);
            errors++;
        }
    }

    if (errors == 0)
        printf("  [PASS] All %u elements correct (%.4f + %.4f = %.4f)\n",
               N, inA[0], inB[0], result[0]);
    else
        printf("  [FAIL] %u / %u elements wrong\n", errors, N);

    // Cleanup
    vkDestroyPipeline(device, pipeline2, nullptr);
    vkDestroyPipelineLayout(device, layout2, nullptr);
    vkDestroyDescriptorSetLayout(device, dsl, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyShaderModule(device, sm, nullptr);
    vkDestroyBuffer(device, bufA, nullptr);
    vkDestroyBuffer(device, bufB, nullptr);
    vkDestroyBuffer(device, bufOut, nullptr);
    vkFreeMemory(device, memA, nullptr);
    vkFreeMemory(device, memB, nullptr);
    vkFreeMemory(device, memOut, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
}

// -----------------------------------------------------------------------
// Test 2: Matrix Multiplication (16x16)
// -----------------------------------------------------------------------
static void test_mat_mul(VkPhysicalDevice physDev, VkDevice device, VkQueue queue, uint32_t qf) {
    printf("\n--- [Test 2] Matrix Multiplication (16x16) ---\n");
    const uint32_t N = 16;
    const VkDeviceSize bufSize = N * N * sizeof(float);

    std::mt19937 rng(123);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

    std::vector<float> matA(N*N), matB(N*N), expected(N*N, 0);
    for (uint32_t i = 0; i < N * N; i++) {
        matA[i] = dist(rng);
        matB[i] = dist(rng);
    }
    for (uint32_t r = 0; r < N; r++)
        for (uint32_t c = 0; c < N; c++)
            for (uint32_t k = 0; k < N; k++)
                expected[r * N + c] += matA[r * N + k] * matB[k * N + c];

    // Shader module
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = compute_mul_spv_size;
    smci.pCode = compute_mul_spv;

    VkShaderModule sm;
    CHK(vkCreateShaderModule(device, &smci, nullptr, &sm), "Create shader module");

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = sm;
    stage.pName = "main";

    // Push constant for matrix dimension
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.size = 4; // int n

    VkDescriptorSetLayoutBinding bindings[3] = {};
    for (int i = 0; i < 3; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 3;
    dslci.pBindings = bindings;
    VkDescriptorSetLayout dsl;
    CHK(vkCreateDescriptorSetLayout(device, &dslci, nullptr, &dsl), "Create DSL");

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout layout;
    CHK(vkCreatePipelineLayout(device, &plci, nullptr, &layout), "Create pipeline layout");

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = layout;
    VkPipeline pipeline;
    CHK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline),
        "Create compute pipeline");

    // Buffers
    VkDeviceMemory memA, memB, memC;
    VkBuffer bufA = make_buffer(physDev, device, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memA, UINT32_MAX);
    VkBuffer bufB = make_buffer(physDev, device, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memB, UINT32_MAX);
    VkBuffer bufC = make_buffer(physDev, device, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memC, UINT32_MAX);

    upload(device, memA, matA.data(), bufSize);
    upload(device, memB, matB.data(), bufSize);

    // Descriptor pool + set
    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps.descriptorCount = 3;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    VkDescriptorPool pool;
    CHK(vkCreateDescriptorPool(device, &dpci, nullptr, &pool), "Create pool");

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    VkDescriptorSet ds;
    CHK(vkAllocateDescriptorSets(device, &dsai, &ds), "Alloc ds");

    VkDescriptorBufferInfo dbi[3] = {};
    dbi[0].buffer = bufA; dbi[0].range = VK_WHOLE_SIZE;
    dbi[1].buffer = bufB; dbi[1].range = VK_WHOLE_SIZE;
    dbi[2].buffer = bufC; dbi[2].range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet wds[3] = {};
    for (int i = 0; i < 3; i++) {
        wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[i].dstSet = ds;
        wds[i].dstBinding = i;
        wds[i].descriptorCount = 1;
        wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pBufferInfo = &dbi[i];
    }
    vkUpdateDescriptorSets(device, 3, wds, 0, nullptr);

    // Command buffer
    VkCommandPoolCreateInfo cpci_cmd{};
    cpci_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci_cmd.queueFamilyIndex = qf;
    cpci_cmd.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cmdPool;
    CHK(vkCreateCommandPool(device, &cpci_cmd, nullptr, &cmdPool), "Create pool");
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    CHK(vkAllocateCommandBuffers(device, &cbai, &cmd), "Alloc cmd");

    // Warmup
    int32_t n = N;
    for (int w = 0; w < 3; w++) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &ds, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
        vkCmdDispatch(cmd, N/16, N/16, 1);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFence fence;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device, &fci, nullptr, &fence);
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
        vkResetCommandPool(device, cmdPool, 0);
    }

    // Timed run
    auto t0 = std::chrono::high_resolution_clock::now();
    const int ITERS = 50;
    for (int iter = 0; iter < ITERS; iter++) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &ds, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
        vkCmdDispatch(cmd, N/16, N/16, 1);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFence fence;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device, &fci, nullptr, &fence);
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
        vkResetCommandPool(device, cmdPool, 0);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Read back
    std::vector<float> result(N*N);
    download(device, memC, result.data(), bufSize);

    // Verify
    uint32_t errors = 0;
    for (uint32_t i = 0; i < N*N; i++) {
        float diff = std::fabs(result[i] - expected[i]);
        float maxVal = (std::max)(1.0f, std::fabs(expected[i]));
        if (diff / maxVal > 0.01f) {
            if (errors < 5) {
                uint32_t r = i / N, c = i % N;
                printf("  MISMATCH [%u,%u]: expected %.2f, got %.2f\n",
                       r, c, expected[i], result[i]);
            }
            errors++;
        }
    }

    if (errors == 0)
        printf("  [PASS] %ux%u mat mul correct, %.2f us/matmul\n",
               N, N, ms_total * 1000.0 / ITERS);
    else
        printf("  [FAIL] %u / %u elements wrong\n", errors, N*N);

    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device, dsl, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyShaderModule(device, sm, nullptr);
    vkDestroyBuffer(device, bufA, nullptr);
    vkDestroyBuffer(device, bufB, nullptr);
    vkDestroyBuffer(device, bufC, nullptr);
    vkFreeMemory(device, memA, nullptr);
    vkFreeMemory(device, memB, nullptr);
    vkFreeMemory(device, memC, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
}

// -----------------------------------------------------------------------
// Test 3: Buffer Device Address query
// -----------------------------------------------------------------------
static void test_buffer_address(VkDevice device) {
    printf("\n--- [Test 3] Buffer Device Address ---\n");
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = 256;
    bci.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buf;
    CHK(vkCreateBuffer(device, &bci, nullptr, &buf), "Create buffer");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, buf, &mr);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    // Find a memory type that supports device address
    VkPhysicalDeviceMemoryProperties pmp;
    vkGetPhysicalDeviceMemoryProperties(
        reinterpret_cast<VkPhysicalDevice>(device), &pmp);
    mai.memoryTypeIndex = 0; // just use 0
    for (uint32_t i = 0; i < pmp.memoryTypeCount; i++) {
        if (mr.memoryTypeBits & (1 << i)) { mai.memoryTypeIndex = i; break; }
    }

    VkMemoryAllocateFlagsInfo mafi{};
    mafi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    mafi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    mai.pNext = &mafi;

    VkDeviceMemory mem;
    CHK(vkAllocateMemory(device, &mai, nullptr, &mem), "Alloc memory");
    CHK(vkBindBufferMemory(device, buf, mem, 0), "Bind");

    VkBufferDeviceAddressInfo bdai{};
    bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bdai.buffer = buf;
    VkDeviceAddress addr = vkGetBufferDeviceAddress(device, &bdai);
    if (addr != 0)
        printf("  [PASS] Buffer device address: 0x%llx\n", (unsigned long long)addr);
    else
        printf("  [WARN] Buffer device address is 0 (may be expected on some configs)\n");

    vkDestroyBuffer(device, buf, nullptr);
    vkFreeMemory(device, mem, nullptr);
}

// =======================================================================
int main() {
    std::atexit(wait_on_exit);
    printf("=== OmniGPU Compute Test ===\n");
    printf("Tests: vector addition, matrix multiplication, buffer addresses\n\n");

    // 1. Create instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "OmniGPU Compute Test";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    CHK(vkCreateInstance(&instInfo, nullptr, &instance), "Create instance");
    printf("[OK] Instance created\n");

    // 2. Enumerate devices
    uint32_t devCount = 0;
    CHK(vkEnumeratePhysicalDevices(instance, &devCount, nullptr), "Enumerate count");
    if (devCount == 0) { printf("No physical devices found\n"); return 1; }

    std::vector<VkPhysicalDevice> devices(devCount);
    CHK(vkEnumeratePhysicalDevices(instance, &devCount, devices.data()), "Enumerate");

    VkPhysicalDevice physDev = devices[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physDev, &props);
    printf("GPU: %s (type=%u, api=%u.%u.%u)\n",
           props.deviceName, props.deviceType,
           VK_API_VERSION_MAJOR(props.apiVersion),
           VK_API_VERSION_MINOR(props.apiVersion),
           VK_API_VERSION_PATCH(props.apiVersion));

    // 3. Find compute queue
    int qf = find_compute_qf(physDev);
    if (qf < 0) { printf("No compute queue family\n"); return 1; }
    printf("Compute queue family: %d\n", qf);

    // 4. Create device
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = static_cast<uint32_t>(qf);
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    // Enable buffer device address
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pNext = &features12;

    VkDevice device;
    CHK(vkCreateDevice(physDev, &dci, nullptr, &device), "Create device");
    printf("[OK] Device created\n");

    VkQueue queue;
    vkGetDeviceQueue(device, qf, 0, &queue);
    printf("[OK] Got compute queue\n");

    // 5. Run tests
    test_vector_add(physDev, device, queue, static_cast<uint32_t>(qf));
    test_mat_mul(physDev, device, queue, static_cast<uint32_t>(qf));
    test_buffer_address(device);

    // 6. Cleanup
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    printf("\n=== ALL TESTS COMPLETED ===\n");
    return 0;
}
