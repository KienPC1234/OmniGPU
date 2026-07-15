#include "nvenc_encoder.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#define NVENCAPI __stdcall
#define NvEncodeAPICreateInstance         nv_nvenc_skip_NvEncodeAPICreateInstance
#define NvEncodeAPIGetMaxSupportedVersion nv_nvenc_skip_NvEncodeAPIGetMaxSupportedVersion
#define NvEncOpenEncodeSessionEx          nv_nvenc_skip_NvEncOpenEncodeSessionEx
#define NvEncDestroyEncoder               nv_nvenc_skip_NvEncDestroyEncoder
#define NvEncInitializeEncoder            nv_nvenc_skip_NvEncInitializeEncoder
#define NvEncCreateInputBuffer            nv_nvenc_skip_NvEncCreateInputBuffer
#define NvEncDestroyInputBuffer           nv_nvenc_skip_NvEncDestroyInputBuffer
#define NvEncCreateBitstreamBuffer        nv_nvenc_skip_NvEncCreateBitstreamBuffer
#define NvEncDestroyBitstreamBuffer       nv_nvenc_skip_NvEncDestroyBitstreamBuffer
#define NvEncEncodePicture                nv_nvenc_skip_NvEncEncodePicture
#define NvEncLockBitstream                nv_nvenc_skip_NvEncLockBitstream
#define NvEncUnlockBitstream              nv_nvenc_skip_NvEncUnlockBitstream
#define NvEncLockInputBuffer              nv_nvenc_skip_NvEncLockInputBuffer
#define NvEncUnlockInputBuffer            nv_nvenc_skip_NvEncUnlockInputBuffer
#define NvEncGetEncodePresetConfigEx      nv_nvenc_skip_NvEncGetEncodePresetConfigEx
#include "../../third_party/nvEncodeAPI.h"
#undef NvEncodeAPICreateInstance
#undef NvEncodeAPIGetMaxSupportedVersion
#undef NvEncOpenEncodeSessionEx
#undef NvEncDestroyEncoder
#undef NvEncInitializeEncoder
#undef NvEncCreateInputBuffer
#undef NvEncDestroyInputBuffer
#undef NvEncCreateBitstreamBuffer
#undef NvEncDestroyBitstreamBuffer
#undef NvEncEncodePicture
#undef NvEncLockBitstream
#undef NvEncUnlockBitstream
#undef NvEncLockInputBuffer
#undef NvEncUnlockInputBuffer
#undef NvEncGetEncodePresetConfigEx

namespace omnigpu {

static std::string str_tolower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------------------
// NVENC API wrapper
// ---------------------------------------------------------------------------
struct NvencApi {
    HMODULE lib = nullptr;
    NV_ENCODE_API_FUNCTION_LIST flist{};
    uint32_t maxVersion = 0;

    bool load() {
        if (lib) return true;
#ifdef _WIN32
        lib = LoadLibraryA("nvEncodeAPI64.dll");
        if (!lib) return false;

        auto createFn = (NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*))
            GetProcAddress(lib, "NvEncodeAPICreateInstance");
        auto getMaxVerFn = (NVENCSTATUS(NVENCAPI*)(uint32_t*))
            GetProcAddress(lib, "NvEncodeAPIGetMaxSupportedVersion");
        if (!createFn) { FreeLibrary(lib); lib = nullptr; return false; }

        memset(&flist, 0, sizeof(flist));
        flist.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        if (createFn(&flist) != NV_ENC_SUCCESS) {
            FreeLibrary(lib); lib = nullptr; return false;
        }
        if (getMaxVerFn) getMaxVerFn(&maxVersion);
#else
        lib = (HMODULE)dlopen("libnvidia-encode.so.1", RTLD_LAZY);
        if (!lib) return false;

        auto createFn = (NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*))
            dlsym(lib, "NvEncodeAPICreateInstance");
        auto getMaxVerFn = (NVENCSTATUS(NVENCAPI*)(uint32_t*))
            dlsym(lib, "NvEncodeAPIGetMaxSupportedVersion");
        if (!createFn) { dlclose(lib); lib = nullptr; return false; }

        memset(&flist, 0, sizeof(flist));
        flist.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        if (createFn(&flist) != NV_ENC_SUCCESS) {
            dlclose(lib); lib = nullptr; return false;
        }
        if (getMaxVerFn) getMaxVerFn(&maxVersion);
#endif
        return true;
    }

    ~NvencApi() {
        if (lib) {
#ifdef _WIN32
            FreeLibrary(lib);
#else
            dlclose(lib);
#endif
        }
    }
};

static NvencApi& g_nvenc() {
    static NvencApi api;
    static bool loaded = false;
    if (!loaded) loaded = api.load();
    return api;
}

// ---------------------------------------------------------------------------
// Helpers: map string config to NVENC enums
// ---------------------------------------------------------------------------
static GUID resolve_preset_guid(const std::string& preset) {
    std::string p = str_tolower(preset);
    if (p == "p1" || p == "default") return NV_ENC_PRESET_P1_GUID;
    if (p == "p2") return NV_ENC_PRESET_P2_GUID;
    if (p == "p3") return NV_ENC_PRESET_P3_GUID;
    if (p == "p4") return NV_ENC_PRESET_P4_GUID;
    if (p == "p5") return NV_ENC_PRESET_P5_GUID;
    if (p == "p6") return NV_ENC_PRESET_P6_GUID;
    if (p == "p7") return NV_ENC_PRESET_P7_GUID;
    SPDLOG_WARN("NVENC: Unknown preset '{}', using P1", preset);
    return NV_ENC_PRESET_P1_GUID;
}

static NV_ENC_TUNING_INFO resolve_tuning_info(const std::string& tuning) {
    std::string t = str_tolower(tuning);
    if (t == "high_quality") return NV_ENC_TUNING_INFO_HIGH_QUALITY;
    if (t == "low_latency") return NV_ENC_TUNING_INFO_LOW_LATENCY;
    if (t == "ultra_low_latency") return NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
    if (t == "lossless") return NV_ENC_TUNING_INFO_LOSSLESS;
    if (t == "ultra_high_quality") return NV_ENC_TUNING_INFO_ULTRA_HIGH_QUALITY;
    SPDLOG_WARN("NVENC: Unknown tuning '{}', using low_latency", tuning);
    return NV_ENC_TUNING_INFO_LOW_LATENCY;
}

// ---------------------------------------------------------------------------
// NvencEncoder Implementation
// ---------------------------------------------------------------------------
struct NvencEncoder::Impl {
    void* encoder = nullptr;
    NV_ENC_INPUT_PTR inputBuffer = nullptr;
    NV_ENC_OUTPUT_PTR outputBuffer = nullptr;
#ifdef _WIN32
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    int bitrateKbps = 0;
    int fps = 30;
    bool initialized = false;
    int64_t frameIdx = 0;
    std::vector<uint8_t> nv12Buffer;
    // User-configurable settings
    NvencSettings settings;
};

NvencEncoder::NvencEncoder() : impl_(std::make_unique<Impl>()) {}
NvencEncoder::~NvencEncoder() { shutdown(); }

void NvencEncoder::set_settings(const NvencSettings& s) { impl_->settings = s; }

bool NvencEncoder::available() const {
    auto& api = g_nvenc();
    if (!api.lib || !api.flist.nvEncOpenEncodeSessionEx) return false;
    if (api.maxVersion > 0) {
        SPDLOG_INFO("NVENC: Driver supports API v{}.{}",
                     api.maxVersion & 0xFF, (api.maxVersion >> 24) & 0xFF);
    }
    SPDLOG_INFO("NVENC: API initialized");
    return true;
}

std::string NvencEncoder::name() const { return "NVENC"; }

static void rgba_to_nv12(const std::vector<uint8_t>& rgba,
                          std::vector<uint8_t>& nv12,
                          uint32_t width, uint32_t height) {
    size_t ySize = static_cast<size_t>(width) * height;
    size_t uvSize = ySize / 2;
    nv12.resize(ySize + uvSize);
    auto* y = nv12.data();
    auto* uv = nv12.data() + ySize;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            size_t rgbaIdx = (static_cast<size_t>(row) * width + col) * 4;
            uint8_t r = rgba[rgbaIdx];
            uint8_t g = rgba[rgbaIdx + 1];
            uint8_t b = rgba[rgbaIdx + 2];
            int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y[static_cast<size_t>(row) * width + col] =
                static_cast<uint8_t>(Y < 0 ? 0 : Y > 255 ? 255 : Y);
            if (row % 2 == 0 && col % 2 == 0) {
                int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                uv[(row / 2) * width + col] =
                    static_cast<uint8_t>(U < 0 ? 0 : U > 255 ? 255 : U);
                uv[(row / 2) * width + col + 1] =
                    static_cast<uint8_t>(V < 0 ? 0 : V > 255 ? 255 : V);
            }
        }
    }
}

bool NvencEncoder::init(VideoCodec codec, uint32_t width, uint32_t height,
                         int fps, int bitrateKbps) {
    auto& api = g_nvenc();
    if (!api.flist.nvEncOpenEncodeSessionEx) return false;
    if (impl_->initialized) shutdown();

    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;
    impl_->bitrateKbps = bitrateKbps;

    GUID presetGUID = resolve_preset_guid(impl_->settings.preset);
    NV_ENC_TUNING_INFO tuningInfo = resolve_tuning_info(impl_->settings.tuning);
    uint32_t gopLength = impl_->settings.gop_length <= 0
                             ? NVENC_INFINITE_GOPLENGTH
                             : static_cast<uint32_t>(impl_->settings.gop_length);

    // Create D3D11 device
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
#ifdef _WIN32
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);
    if (FAILED(hr)) {
        SPDLOG_ERROR("NVENC: Failed to create D3D11 device (hr=0x{:X})", (unsigned)hr);
        return false;
    }
#endif

    bool sessionOk = false;
#ifdef _WIN32
    __try {
#endif
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams{};
        memset(&sessionParams, 0, sizeof(sessionParams));
        sessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        sessionParams.apiVersion = NVENCAPI_VERSION;
        sessionParams.device = d3dDevice;
        sessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;

        if (api.flist.nvEncOpenEncodeSessionEx(&sessionParams, &impl_->encoder) != 0) {
            SPDLOG_ERROR("NVENC: Failed to open encode session");
        } else {
            sessionOk = true;
        }
#ifdef _WIN32
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SPDLOG_WARN("NVENC: OpenEncodeSessionEx crashed. Falling back to JPEG.");
    }
#endif
    if (!sessionOk) {
        if (d3dContext) d3dContext->Release();
        if (d3dDevice) d3dDevice->Release();
        return false;
    }

    impl_->d3dDevice = d3dDevice;
    impl_->d3dContext = d3dContext;

    GUID codecGUID = NV_ENC_CODEC_H264_GUID;
    if (codec == VideoCodec::HEVC) codecGUID = NV_ENC_CODEC_HEVC_GUID;

    // Get preset configuration
    NV_ENC_PRESET_CONFIG presetConfig{};
    memset(&presetConfig, 0, sizeof(presetConfig));
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
    NVENCSTATUS ps = api.flist.nvEncGetEncodePresetConfigEx(
        impl_->encoder, codecGUID, presetGUID, tuningInfo, &presetConfig);
    if (ps != NV_ENC_SUCCESS) {
        SPDLOG_ERROR("NVENC: Failed to get preset config (status={})", (int)ps);
        return false;
    }

    // Customize config from user settings
    NV_ENC_CONFIG& encodeConfig = presetConfig.presetCfg;
    encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encodeConfig.rcParams.averageBitRate = bitrateKbps * 1000;
    encodeConfig.rcParams.maxBitRate = bitrateKbps * 1000;
    encodeConfig.rcParams.vbvBufferSize = bitrateKbps * 1000;
    encodeConfig.rcParams.vbvInitialDelay = bitrateKbps * 1000;
    encodeConfig.gopLength = gopLength;
    encodeConfig.frameIntervalP = 1;

    NV_ENC_INITIALIZE_PARAMS initParams{};
    memset(&initParams, 0, sizeof(initParams));
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = codecGUID;
    initParams.presetGUID = presetGUID;
    initParams.encodeWidth = width;
    initParams.encodeHeight = height;
    initParams.darWidth = width;
    initParams.darHeight = height;
    initParams.frameRateNum = fps;
    initParams.frameRateDen = 1;
    initParams.enableEncodeAsync = 0;
    initParams.enablePTD = 1;
    initParams.reportSliceOffsets = 0;
    initParams.enableSubFrameWrite = 0;
    initParams.encodeConfig = &encodeConfig;
    initParams.tuningInfo = tuningInfo;

    NVENCSTATUS initStatus = api.flist.nvEncInitializeEncoder(impl_->encoder, &initParams);
    if (initStatus != NV_ENC_SUCCESS) {
        SPDLOG_ERROR("NVENC: Failed to initialize encoder (status={})", (int)initStatus);
        return false;
    }

    // Create input buffer (NV12)
    NV_ENC_CREATE_INPUT_BUFFER createInput{};
    memset(&createInput, 0, sizeof(createInput));
    createInput.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    createInput.width = width;
    createInput.height = height;
    createInput.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
    createInput.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    if (api.flist.nvEncCreateInputBuffer(impl_->encoder, &createInput) != 0) {
        SPDLOG_ERROR("NVENC: Failed to create input buffer");
        return false;
    }
    impl_->inputBuffer = createInput.inputBuffer;

    // Create output bitstream buffer
    NV_ENC_CREATE_BITSTREAM_BUFFER createOutput{};
    memset(&createOutput, 0, sizeof(createOutput));
    createOutput.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    createOutput.size = 2 * 1024 * 1024;
    createOutput.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
    if (api.flist.nvEncCreateBitstreamBuffer(impl_->encoder, &createOutput) != 0) {
        SPDLOG_ERROR("NVENC: Failed to create bitstream buffer");
        return false;
    }
    impl_->outputBuffer = createOutput.bitstreamBuffer;

    impl_->initialized = true;
    SPDLOG_INFO("NVENC: Encoder initialized ({}x{} @ {}fps, {} kbps, codec={}, preset={}, tuning={}, gop={})",
                 width, height, fps, bitrateKbps,
                 codec == VideoCodec::HEVC ? "HEVC" : "H264",
                 impl_->settings.preset, impl_->settings.tuning,
                 gopLength == NVENC_INFINITE_GOPLENGTH ? 0 : (int)gopLength);
    return true;
}

void NvencEncoder::shutdown() {
    if (!impl_->initialized) return;
    auto& api = g_nvenc();

    if (impl_->outputBuffer && api.flist.nvEncDestroyBitstreamBuffer) {
        api.flist.nvEncDestroyBitstreamBuffer(impl_->encoder, impl_->outputBuffer);
        impl_->outputBuffer = nullptr;
    }
    if (impl_->inputBuffer && api.flist.nvEncDestroyInputBuffer) {
        api.flist.nvEncDestroyInputBuffer(impl_->encoder, impl_->inputBuffer);
        impl_->inputBuffer = nullptr;
    }
    if (impl_->encoder && api.flist.nvEncDestroyEncoder) {
        api.flist.nvEncDestroyEncoder(impl_->encoder);
        impl_->encoder = nullptr;
    }
#ifdef _WIN32
    if (impl_->d3dContext) { impl_->d3dContext->Release(); impl_->d3dContext = nullptr; }
    if (impl_->d3dDevice) { impl_->d3dDevice->Release(); impl_->d3dDevice = nullptr; }
#endif

    impl_->initialized = false;
    SPDLOG_INFO("NVENC encoder shut down");
}

bool NvencEncoder::encode(const std::vector<uint8_t>& rgba,
                           std::vector<EncodedPacket>& packets) {
    auto& api = g_nvenc();
    auto& impl = impl_;

    rgba_to_nv12(rgba, impl->nv12Buffer, impl->width, impl->height);

    NV_ENC_LOCK_INPUT_BUFFER lockInput{};
    memset(&lockInput, 0, sizeof(lockInput));
    lockInput.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lockInput.inputBuffer = impl->inputBuffer;
    if (api.flist.nvEncLockInputBuffer(impl->encoder, &lockInput) != 0) {
        return false;
    }

    auto* src = impl->nv12Buffer.data();
    auto* dst = static_cast<uint8_t*>(lockInput.bufferDataPtr);
    uint32_t pitch = lockInput.pitch;
    size_t ySize = static_cast<size_t>(impl->width) * impl->height;

    for (uint32_t row = 0; row < impl->height; ++row) {
        memcpy(dst + static_cast<size_t>(row) * pitch,
               src + static_cast<size_t>(row) * impl->width,
               impl->width);
    }
    size_t uvOffset = static_cast<size_t>(impl->height) * pitch;
    for (uint32_t row = 0; row < impl->height / 2; ++row) {
        memcpy(dst + uvOffset + static_cast<size_t>(row) * pitch,
               src + ySize + static_cast<size_t>(row) * impl->width,
               impl->width);
    }

    api.flist.nvEncUnlockInputBuffer(impl->encoder, impl->inputBuffer);

    NV_ENC_PIC_PARAMS picParams{};
    memset(&picParams, 0, sizeof(picParams));
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputBuffer = impl->inputBuffer;
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    picParams.inputWidth = impl->width;
    picParams.inputHeight = impl->height;
    picParams.inputPitch = pitch;
    picParams.outputBitstream = impl->outputBuffer;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picParams.pictureType = NV_ENC_PIC_TYPE_P;
    picParams.frameIdx = impl->frameIdx++;
    picParams.inputDuration = 0;
    picParams.inputTimeStamp = static_cast<uint64_t>(impl->frameIdx);
    picParams.encodePicFlags = 0;
    picParams.completionEvent = nullptr;
    picParams.qpDeltaMap = nullptr;
    picParams.qpDeltaMapSize = 0;

    if (api.flist.nvEncEncodePicture(impl->encoder, &picParams) != 0) {
        return false;
    }

    NV_ENC_LOCK_BITSTREAM lockOutput{};
    memset(&lockOutput, 0, sizeof(lockOutput));
    lockOutput.version = NV_ENC_LOCK_BITSTREAM_VER;
    lockOutput.outputBitstream = impl->outputBuffer;
    if (api.flist.nvEncLockBitstream(impl->encoder, &lockOutput) != 0) {
        return false;
    }

    EncodedPacket packet;
    packet.data.assign(
        static_cast<const uint8_t*>(lockOutput.bitstreamBufferPtr),
        static_cast<const uint8_t*>(lockOutput.bitstreamBufferPtr) +
            lockOutput.bitstreamSizeInBytes);
    packet.isKeyframe = (picParams.encodePicFlags & NV_ENC_PIC_FLAG_FORCEIDR) != 0;
    packet.pts = picParams.frameIdx;
    packets.push_back(std::move(packet));

    api.flist.nvEncUnlockBitstream(impl->encoder, impl->outputBuffer);
    return true;
}

bool NvencEncoder::flush(std::vector<EncodedPacket>& packets) {
    (void)packets;
    return true;
}

} // namespace omnigpu
