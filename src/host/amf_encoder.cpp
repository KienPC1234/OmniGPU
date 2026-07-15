#include "amf_encoder.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <mutex>
#include <vector>

namespace omnigpu {

using AMFInitFunc = int64_t (__cdecl *)(uint64_t, void**);

static void* g_amfLib = nullptr;
static AMFInitFunc g_amfInit = nullptr;
static std::once_flag g_amfLoadFlag;

static void load_amf_library() {
    std::call_once(g_amfLoadFlag, []{
#ifdef _WIN32
        g_amfLib = LoadLibraryA("amfrt64.dll");
        if (!g_amfLib) g_amfLib = LoadLibraryA("amfrtlt64.dll");
        if (g_amfLib) {
            void* p = (void*)GetProcAddress((HMODULE)g_amfLib, "AMFInit");
            memcpy(&g_amfInit, &p, sizeof(p));
        }
#else
        g_amfLib = dlopen("libamfrt64.so.1", RTLD_LAZY);
        if (!g_amfLib) g_amfLib = dlopen("libamfrt.so", RTLD_LAZY);
        if (g_amfLib) {
            void* p = dlsym(g_amfLib, "AMFInit");
            memcpy(&g_amfInit, &p, sizeof(p));
        }
#endif
    });
}

static void unload_amf_library() {
    if (g_amfLib) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(g_amfLib));
#else
        dlclose(g_amfLib);
#endif
        g_amfLib = nullptr;
        g_amfInit = nullptr;
    }
}

struct AmfEncoder::Impl {
    uint32_t width = 0;
    uint32_t height = 0;
    int fps = 30;
    int bitrateKbps = 0;
    bool initialized = false;
    int64_t frameIdx = 0;
};

AmfEncoder::AmfEncoder() : impl_(std::make_unique<Impl>()) {}
AmfEncoder::~AmfEncoder() { shutdown(); }

bool AmfEncoder::available() const {
    load_amf_library();
    if (!g_amfInit) return false;
    // AMF detected but needs full SDK headers for actual encoding.
    // See: https://github.com/GPUOpen-LibrariesAndSDKs/AMF
    // Headers downloaded at third_party/AMF/include/
    // TODO: implement with AMF SDK headers for real HW encoding
    return false;
}

std::string AmfEncoder::name() const { return "AMF"; }

bool AmfEncoder::init(VideoCodec codec, uint32_t width, uint32_t height,
                       int fps, int bitrateKbps) {
    if (impl_->initialized) shutdown();
    load_amf_library();
    if (!g_amfInit) {
        SPDLOG_ERROR("AMF: runtime not available");
        return false;
    }
    SPDLOG_WARN("AMF: detected but not fully implemented. "
                "SDK headers at third_party/AMF/include/");
    return false;
}

bool AmfEncoder::encode(const std::vector<uint8_t>& rgba,
                         std::vector<EncodedPacket>& packets) {
    return false;
}

bool AmfEncoder::flush(std::vector<EncodedPacket>&) { return false; }

void AmfEncoder::shutdown() {
    impl_->initialized = false;
    SPDLOG_INFO("AMF encoder shut down");
}

} // namespace omnigpu
