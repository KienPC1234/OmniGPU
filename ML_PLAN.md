# Kế Hoạch Hỗ Trợ ML cho OmniGPU — Chi Tiết Từng Dòng Code

> **Đã verify với `vk.xml`**: Tất cả struct name, field name, extension number đúng Vulkan spec.

## ✅ VERIFY RESULTS (từ `third_party/Vulkan-Headers/registry/vk.xml`)

| Item | Spec | ML_PLAN.md | Match? |
|------|------|-----------|--------|
| Extension: float16_int8 | `VK_KHR_shader_float16_int8` #83 | ✓ | ✅ |
| Extension: 16bit_storage | `VK_KHR_16bit_storage` #84 | ✓ | ✅ |
| Extension: 8bit_storage | `VK_KHR_8bit_storage` #178 | ✓ | ✅ |
| Extension: cooperative_matrix | `VK_KHR_cooperative_matrix` #507 | ✓ | ✅ |
| Struct: float16 features | `VkPhysicalDeviceShaderFloat16Int8FeaturesKHR` | ✓ | ✅ |
| Struct: 16bit features | `VkPhysicalDevice16BitStorageFeatures` | ✓ | ✅ |
| Struct: 8bit features | `VkPhysicalDevice8BitStorageFeatures` | ✓ | ✅ |
| Struct: coopmat features | `VkPhysicalDeviceCooperativeMatrixFeaturesKHR` | ✓ | ✅ |
| Feature: shaderFloat16 | aliased to `VkPhysicalDeviceVulkan12Features` | Set in f12 ✅ | ✅ |
| Feature: shaderInt8 | aliased to `VkPhysicalDeviceVulkan12Features` | Set in f12 ✅ | ✅ |

### ⚠️ Phát hiện: `VK_KHR_8bit_storage` promoted to Vulkan 1.2

Theo `vk.xml` line 33997: `8bit_storage` **đã promote vào Vulkan 1.2**. Các feature `storageBuffer8BitAccess` và `uniformAndStorageBuffer8BitAccess` có sẵn trong `VkPhysicalDeviceVulkan12Features`.

→ **Cần bổ sung**: Thêm `storageBuffer8BitAccess` + `uniformAndStorageBuffer8BitAccess` vào Bước 7a (VkPhysicalDeviceVulkan12Features block), bên cạnh case riêng trong pNext chain. Đảm bảo hoạt động cả với Vulkan 1.2+ (qua f12 struct) và Vulkan 1.1 (qua pNext extension struct).

---

## PHASE 1: llamba.cpp CHẠY ĐƯỢC (3-4h)

### Bước 1: Thêm 3 field vào `gpu_caps.h`
**File:** `src/common/gpu_caps.h`, sau dòng 68 (`supported_subgroup_operations`):

```cpp
    // ===== ML support flags (NEW - Phase 1) =====
    bool supports_16bit_storage       = false;
    bool supports_8bit_storage        = false;
    bool supports_float16_int8        = false;
```

---

### Bước 2: Thêm 3 field vào schema `.fbs`
**File:** `src/schemas/omnigpu_protocol.fbs`, sau dòng 368 (`buffer_manager_capable`):

```fbs
  // ===== ML support flags (NEW - Phase 1) =====
  supports_16bit_storage: bool = false;
  supports_8bit_storage: bool = false;
  supports_float16_int8: bool = false;
```

**Sau đó:** Regenerate FlatBuffers C++ code:
```powershell
cd C:\Users\kien\Documents\repos\OmniGPU
flatc --cpp -o src/schemas/generated src/schemas/omnigpu_protocol.fbs
```

---

### Bước 3: Query host GPU thật trong `handshake.cpp`
**File:** `src/host/handshake.cpp`

**3a. Sửa `query_subgroup_ops()` → `query_subgroup_info()` (line 44-52):**

XÓA function cũ, THAY bằng:
```cpp
struct SubgroupInfo {
    uint32_t subgroupSize = 32;
    uint32_t supportedOperations = 0;
    uint32_t supportedStages = VK_SHADER_STAGE_ALL;
};

static SubgroupInfo query_subgroup_info(VkPhysicalDevice physDev) {
    SubgroupInfo info;
    VkPhysicalDeviceSubgroupProperties subProps{};
    subProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &subProps;
    vkGetPhysicalDeviceProperties2(physDev, &props2);
    info.subgroupSize = subProps.subgroupSize;
    info.supportedOperations = static_cast<uint32_t>(subProps.supportedOperations);
    info.supportedStages = static_cast<uint32_t>(subProps.supportedStages);
    return info;
}
```

**3b. Sửa dòng 140 — subgroup_ops → subgroup_info:**

XÓA dòng 140:
```cpp
caps.supported_subgroup_operations = query_subgroup_ops(physDevice);
```
THAY bằng:
```cpp
auto subInfo = query_subgroup_info(physDevice);
caps.supported_subgroup_operations = subInfo.supportedOperations;
```

**3c. Sửa dòng 150 — BUG subgroup_size:**

XÓA dòng 150:
```cpp
caps.subgroup_size = query_subgroup_ops(physDevice);  // BUG: đây là supportedOperations mask!
```
THAY bằng:
```cpp
caps.subgroup_size = subInfo.subgroupSize;  // subgroupSize thật từ GPU
```

**3d. Thêm query 16-bit/8-bit/float16-int8 support — vào giữa dòng 139 và 140:**

```cpp
// ===== ML support: query optional device features via pNext chain =====
{
    VkPhysicalDevice16BitStorageFeatures f16{};
    f16.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;

    VkPhysicalDevice8BitStorageFeatures f8{};
    f8.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES;
    f16.pNext = &f8;

    VkPhysicalDeviceShaderFloat16Int8Features ff16{};
    ff16.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
    f8.pNext = &ff16;

    VkPhysicalDeviceFeatures2 feat2{};
    feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat2.pNext = &f16;
    vkGetPhysicalDeviceFeatures2(physDevice, &feat2);

    caps.supports_16bit_storage = f16.storageBuffer16BitAccess;
    caps.supports_8bit_storage  = f8.storageBuffer8BitAccess;
    caps.supports_float16_int8  = ff16.shaderFloat16 && ff16.shaderInt8;
}
```

---

### Bước 4: Gửi 3 field trong `CreateCapabilitiesResponse` (handshake.cpp)
**File:** `src/host/handshake.cpp`, thêm vào sau dòng 273 (cuối `CreateCapabilitiesResponse` call, trước `);`):

```cpp
        caps.supports_16bit_storage,
        caps.supports_8bit_storage,
        caps.supports_float16_int8
```
Dòng 273 hiện là `true  // buffer_manager_capable` — thêm dấu phẩy và 3 dòng mới:
```cpp
        true,  // buffer_manager_capable
        caps.supports_16bit_storage,    // NEW Phase 1
        caps.supports_8bit_storage,     // NEW Phase 1
        caps.supports_float16_int8      // NEW Phase 1
```

---

### Bước 5: Parse 3 field trong guest_init.cpp
**File:** `src/guest/guest_init.cpp`, thêm sau dòng 169 `caps->max_tessellation_factor()`:

```cpp
    // ===== ML support flags (NEW - Phase 1) =====
    gpu_caps.supports_16bit_storage = caps->supports_16bit_storage();
    gpu_caps.supports_8bit_storage  = caps->supports_8bit_storage();
    gpu_caps.supports_float16_int8  = caps->supports_float16_int8();
```

---

### Bước 6: Advertise extension trong guest
**File:** `src/guest/vk_intercept.cpp`, dòng 329 (trong `vkEnumerateDeviceExtensionProperties_hook`):

THÊM sau dòng 329 (`add("VK_KHR_8bit_storage", 1);`):
```cpp
        add("VK_KHR_shader_float16_int8", 1);
```

---

### Bước 7: Enable features dựa trên caps trong `vkGetPhysicalDeviceFeatures2_hook`
**File:** `src/guest/vk_intercept.cpp`, dòng 702-731

**7a. Thêm `shaderFloat16` + `shaderInt8` vào Vulkan 1.2 features (sau dòng 719):**

Thêm vào giữa `f12->drawIndirectCount = VK_TRUE;` (dòng 719) và `f12->samplerMirrorClampToEdge...` (dòng 721):
```cpp
            // ML support — enable only if host GPU supports them
            f12->shaderFloat16 = caps.valid() ? caps.supports_float16_int8 : VK_FALSE;
            f12->shaderInt8   = caps.valid() ? caps.supports_float16_int8 : VK_FALSE;
```

**7b. Thêm 3 case mới vào pNext chain switch (sau dòng 747, trước `default:`):**

```cpp
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            auto* f16s = reinterpret_cast<VkPhysicalDevice16BitStorageFeatures*>(ext);
            bool ok = caps.valid() && caps.supports_16bit_storage;
            f16s->storageBuffer16BitAccess = ok;
            f16s->uniformAndStorageBuffer16BitAccess = ok;
            f16s->storagePushConstant16 = VK_FALSE;
            f16s->storageInputOutput16 = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: {
            auto* f8s = reinterpret_cast<VkPhysicalDevice8BitStorageFeatures*>(ext);
            bool ok = caps.valid() && caps.supports_8bit_storage;
            f8s->storageBuffer8BitAccess = ok;
            f8s->uniformAndStorageBuffer8BitAccess = ok;
            f8s->storagePushConstant8 = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
            auto* ffi = reinterpret_cast<VkPhysicalDeviceShaderFloat16Int8Features*>(ext);
            bool ok = caps.valid() && caps.supports_float16_int8;
            ffi->shaderFloat16 = ok;
            ffi->shaderInt8 = ok;
            break;
        }
```

---

### Bước 8: Build & Test
```powershell
cd C:\Users\kien\Documents\repos\OmniGPU
.\scripts\windows\build-and-package.ps1
python -c "..." # deploy guest DLL to VM, restart host
```

**Test với llama.cpp:**
```powershell
# Trên VM test (192.168.1.113):
cd C:\llama.cpp
.\build\bin\Release\llama-cli.exe -m C:\models\TinyLlama-1.1B-Q4_K_M.gguf -ngl 99 -p "Hello" -n 50
```

**Verify logs:**
- Guest log: `"Host GPU capabilities: NVIDIA GeForce RTX 4090 (supports_float16_int8=1)"`
- GGML log: `"Vulkan0: NVIDIA GeForce RTX 4090 | uma: 0 | fp16: 1 | warp size: 32"`
- Inference produces output tokens (không crash, không NaN)

---

## PHASE 2: TENSOR CORE + DOT PRODUCT (4-6h)

### Bước 1: Thêm caps fields cho cooperative matrix
**File:** `src/common/gpu_caps.h`, sau 3 field Phase 1:

```cpp
    // ===== Cooperative matrix (Phase 2) =====
    bool supports_cooperative_matrix   = false;
    uint32_t coopmat_m = 16;  // tile size M (rows of A)
    uint32_t coopmat_n = 16;  // tile size N (cols of B)
    uint32_t coopmat_k = 16;  // tile size K (inner dimension)
    bool supports_integer_dot_product = false;
```

### Bước 2: Thêm vào schema
**File:** `omnigpu_protocol.fbs`, sau 3 field Phase 1:

```fbs
  supports_cooperative_matrix: bool = false;
  coopmat_m: uint32 = 16;
  coopmat_n: uint32 = 16;
  coopmat_k: uint32 = 16;
  supports_integer_dot_product: bool = false;
```

### Bước 3: Query host GPU trong handshake.cpp

```cpp
// Query cooperative matrix support (cần VK_KHR_cooperative_matrix extension)
{
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cmf{};
    cmf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 feat2{};
    feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat2.pNext = &cmf;
    vkGetPhysicalDeviceFeatures2(physDevice, &feat2);
    caps.supports_cooperative_matrix = cmf.cooperativeMatrix;

    if (caps.supports_cooperative_matrix) {
        uint32_t propCount = 0;
        vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(physDevice, &propCount, nullptr);
        std::vector<VkCooperativeMatrixPropertiesKHR> props(propCount);
        for (auto& p : props) {
            p.sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
        }
        vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(physDevice, &propCount, props.data());
        // Tìm tile size lớn nhất cho fp16 (ML workloads chính)
        for (const auto& p : props) {
            if (p.AType == VK_COMPONENT_TYPE_FLOAT16_KHR &&
                p.BType == VK_COMPONENT_TYPE_FLOAT16_KHR &&
                p.CType == VK_COMPONENT_TYPE_FLOAT16_KHR) {
                caps.coopmat_m = std::max(caps.coopmat_m, p.MSize);
                caps.coopmat_n = std::max(caps.coopmat_n, p.NSize);
                caps.coopmat_k = std::max(caps.coopmat_k, p.KSize);
            }
        }
    }
}

// Query integer dot product
{
    VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR idpf{};
    idpf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 feat2{};
    feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat2.pNext = &idpf;
    vkGetPhysicalDeviceFeatures2(physDevice, &feat2);
    caps.supports_integer_dot_product = idpf.shaderIntegerDotProduct;
}
```

### Bước 4: Advertise extension + feature trong guest

Thêm vào `vkEnumerateDeviceExtensionProperties_hook`:
```cpp
add("VK_KHR_cooperative_matrix", 1);
add("VK_KHR_shader_integer_dot_product", 1);
```

Thêm case vào `vkGetPhysicalDeviceFeatures2_hook` pNext chain:
```cpp
case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR: {
    auto* f = reinterpret_cast<VkPhysicalDeviceCooperativeMatrixFeaturesKHR*>(ext);
    bool ok = caps.valid() && caps.supports_cooperative_matrix;
    f->cooperativeMatrix = ok;
    break;
}
case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR: {
    auto* f = reinterpret_cast<VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR*>(ext);
    bool ok = caps.valid() && caps.supports_integer_dot_product;
    f->shaderIntegerDotProduct = ok;
    break;
}
```

### Bước 5: Test Phase 2
```powershell
llama-cli -m model.gguf -ngl 99 -p "Hello" -n 100
# Check GGML log: "Using cooperative matrix type: 16x16x16" (không còn "Using matmul shader fallback")
# Benchmark: >20 tok/s với llama 7B Q4_K_M
```

---

## PHASE 3: PIPELINE CACHE + MEMORY + PRODUCTION (6-8h)

### 3.1 Pipeline Cache Persistence
**Vấn đề:** llama.cpp compile ~200 shader lúc khởi động. Không có cache = 10-30s compile mỗi lần chạy.

**Guest fix (vk_intercept.cpp):** `vkGetPipelineCacheData_hook` hiện return size=0. Cần gửi query lên host lấy cache thật.

```cpp
VkResult VKAPI_PTR vkGetPipelineCacheData_hook(
    VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData)
{
    if (!pDataSize) return VK_INCOMPLETE;

    // Query host for actual pipeline cache data
    auto* cl = init::get_client();
    if (cl && cl->socket() != INVALID_SOCKET) {
        // Sync query: host returns cache size first
        uint64_t cacheSize = cl->sync_query(0x90, handle_to_u64(pipelineCache));
        if (!pData) {
            *pDataSize = static_cast<size_t>(cacheSize);
            return VK_SUCCESS;
        }
        if (*pDataSize < cacheSize) {
            *pDataSize = static_cast<size_t>(cacheSize);
            return VK_INCOMPLETE;
        }
        // Second sync query: host sends actual cache bytes
        // (Cần extend sync_query_ext protocol như S28 đã làm)
        *pDataSize = static_cast<size_t>(cacheSize);
        return VK_SUCCESS;
    }
    *pDataSize = 0;
    return VK_SUCCESS;
}
```

**Host handler:** Thêm `vkGetPipelineCacheData` handler trong `command_dispatcher.cpp` — gọi `vkGetPipelineCacheData` trên host GPU, gửi data về guest qua `sendDataFn_`.

**Guest cache file:** Lưu ra `%TEMP%/omnigpu_pipeline_cache.bin`, load lại qua `vkCreatePipelineCache` với `pInitialData`.

### 3.2 Dirty Page Memory Tracking

Áp dụng strategy B1 từ task.md: shadow compare 64KB pages cho coherent memory.
- Giảm bandwidth upload model weights từ GB → chỉ vài MB changed pages mỗi frame.
- Đặc biệt quan trọng cho ML inference: weights thay đổi không thường xuyên.

### 3.3 Multi-GPU (nếu có)
- Enable `MultiGpuCompute::split_work()` cho model parallelism
- Tách batch tokens across GPUs
- Cần sync barrier giữa các GPU

---

## APPENDIX: FILE CHANGE CHECKLIST

```
[ ] gpu_caps.h        — thêm 6 field (Phase 1: 3 + Phase 2: 3)
[ ] omnigpu_protocol.fbs — thêm 6 field + regenerate FlatBuffers
[ ] handshake.cpp     — sửa subgroup bug + query 16/8/float16-int8 + cooperative matrix
[ ] handshake.cpp     — thêm 6 field vào CreateCapabilitiesResponse
[ ] guest_init.cpp    — parse 6 field từ response
[ ] vk_intercept.cpp  — extension list: +shader_float16_int8, +cooperative_matrix, +integer_dot_product
[ ] vk_intercept.cpp  — vkGetPhysicalDeviceFeatures2: thêm 5 case mới vào pNext chain
[ ] vk_intercept.cpp  — shaderFloat16/shaderInt8 trong VkPhysicalDeviceVulkan12Features
[ ] command_dispatcher.cpp — Phase 3: vkGetPipelineCacheData handler
[ ] Build + deploy → test llama.cpp
```

---

## APPENDIX: TROUBLESHOOTING

| Vấn đề | Nguyên nhân | Fix |
|--------|------------|-----|
| GGML: "Vulkan error VK_ERROR_FEATURE_NOT_PRESENT" | `shaderFloat16` chưa set TRUE trong Vulkan 1.2 features | Kiểm tra `vk_intercept.cpp` dòng 720 có `f12->shaderFloat16 = VK_TRUE` |
| GGML: "Unsupported device" ngay khi init | `storageBuffer16BitAccess` chưa có trong pNext chain | Check case `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES` đã thêm vào switch |
| llama.cpp chạy nhưng crash khi dispatch | shader compile fail do thiếu extension | Check extension list có `VK_KHR_shader_float16_int8` |
| Caps không đúng | `subgroup_size` vẫn là supportedOperations mask | Double-check handshake.cpp dòng 150 đã sửa thành `subInfo.subgroupSize` |
| Chậm (không dùng Tensor Core) | Guest không advertise `VK_KHR_cooperative_matrix` | Check extension list + caps.supports_cooperative_matrix=true |
