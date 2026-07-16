#define NOMINMAX
#include "video_decoder.h"
#include "ffmpeg_decoder.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>
#include <turbojpeg.h>

#ifdef _WIN32
#include <windows.h>
#include <mferror.h>
#endif

// LZ4 for software fallback path
#include <lz4.h>

namespace omnigpu::video {

// ==========================================================================
// Win32 display window
// ==========================================================================
#ifdef _WIN32
static HWND g_display_wnd = nullptr;
static HBITMAP g_display_bmp = nullptr;
static HDC g_display_memdc = nullptr;
static uint32_t g_disp_w = 0, g_disp_h = 0;
static bool g_disp_created = false;

static LRESULT CALLBACK display_wnd_proc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY || msg == WM_CLOSE) {
        DestroyWindow(hw);
        g_display_wnd = nullptr;
        g_disp_created = false;
        return 0;
    }
    if (msg == WM_PAINT && g_disp_created) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        if (g_display_bmp && g_display_memdc) {
            HGDIOBJ old = SelectObject(g_display_memdc, g_display_bmp);
            BitBlt(hdc, 0, 0, (int)g_disp_w, (int)g_disp_h,
                   g_display_memdc, 0, 0, SRCCOPY);
            SelectObject(g_display_memdc, old);
        }
        EndPaint(hw, &ps);
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

static void create_display_window(uint32_t w, uint32_t h) {
    if (g_disp_created) return;
    g_disp_w = w; g_disp_h = h;
    HMODULE hinst = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&display_wnd_proc), &hinst);
    WNDCLASSA wc{};
    wc.lpfnWndProc = display_wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = "OmniGPUDisplay";
    RegisterClassA(&wc);
    g_display_wnd = CreateWindowExA(0, "OmniGPUDisplay", "OmniGPU - Remote Display",
                                     WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT,
                                     (int)w + 16, (int)h + 39,
                                     nullptr, nullptr, hinst, nullptr);
    if (g_display_wnd) {
        g_display_memdc = CreateCompatibleDC(nullptr);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)w;
        bmi.bmiHeader.biHeight = -(LONG)h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        g_display_bmp = CreateDIBSection(g_display_memdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ShowWindow(g_display_wnd, SW_SHOW);
        g_disp_created = true;
        SPDLOG_INFO("Display window created ({}x{})", w, h);
    }
}

static void update_display(const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (!g_disp_created || !g_display_bmp || !g_display_memdc) return;
    BITMAP bm{};
    GetObjectA(g_display_bmp, sizeof(bm), &bm);
    if (bm.bmBits && rgba) {
        std::memcpy(bm.bmBits, rgba, (size_t)w * h * 4);
        InvalidateRect(g_display_wnd, nullptr, FALSE);
    }
}

static void destroy_display() {
    if (g_display_bmp) { DeleteObject(g_display_bmp); g_display_bmp = nullptr; }
    if (g_display_memdc) { DeleteDC(g_display_memdc); g_display_memdc = nullptr; }
    if (g_display_wnd) { DestroyWindow(g_display_wnd); g_display_wnd = nullptr; }
    HMODULE hinst = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&display_wnd_proc), &hinst)) {
        UnregisterClassA("OmniGPUDisplay", hinst);
    }
    g_disp_created = false;
}
#else
static void create_display_window(uint32_t, uint32_t) {}
static void update_display(const uint8_t*, uint32_t, uint32_t) {}
static void destroy_display() {}
#endif

// ==========================================================================
// Media Foundation Hardware Decoder (Windows)
// ==========================================================================
#ifdef _WIN32

MfH264Decoder::MfH264Decoder() {}
MfH264Decoder::~MfH264Decoder() { shutdown(); }

bool MfH264Decoder::hardware_accelerated() const { return hw_accel_; }
std::string MfH264Decoder::name() const {
    return hw_accel_ ? "MediaFoundation (DXVA2 HW)" : "MediaFoundation (SW)";
}

bool MfH264Decoder::init() {
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        SPDLOG_WARN("MfH264Decoder: MFStartup failed (0x{:08X})", hr);
        return false;
    }
    SPDLOG_INFO("MfH264Decoder: Media Foundation initialized");
    return true;
}

void MfH264Decoder::shutdown() {
    if (mft_) { mft_->Release(); mft_ = nullptr; }
    if (input_type_) { input_type_->Release(); input_type_ = nullptr; }
    if (output_type_) { output_type_->Release(); output_type_ = nullptr; }
    initialized_ = false;
    MFShutdown();
    destroy_display();
}

bool MfH264Decoder::init_mft(Codec codec, uint32_t w, uint32_t h) {
    if (mft_) { mft_->Release(); mft_ = nullptr; }
    if (input_type_) { input_type_->Release(); input_type_ = nullptr; }
    if (output_type_) { output_type_->Release(); output_type_ = nullptr; }

    // Select MFT category based on codec
    GUID subtype = (codec == Codec::HEVC) ? MFVideoFormat_HEVC : MFVideoFormat_H264;

    MFT_REGISTER_TYPE_INFO input_info = { MFMediaType_Video, subtype };
    IMFActivate** activates = nullptr;
    UINT32 count = 0;

    // Try hardware MFT first (prefer DXVA2)
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                            MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                            &input_info, nullptr, &activates, &count);
    hw_accel_ = SUCCEEDED(hr) && count > 0;

    if (!hw_accel_) {
        // Fall back to software MFT
        hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                        &input_info, nullptr, &activates, &count);
    }

    if (FAILED(hr) || count == 0) {
        SPDLOG_WARN("MfH264Decoder: no MFT found for codec {}",
                     (codec == Codec::HEVC) ? "HEVC" : "H.264");
        if (codec == Codec::HEVC) {
            SPDLOG_WARN("HEVC decoder not available. Install 'HEVC Video Extensions' from Microsoft Store.");
        }
        return false;
    }

    hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mft_));
    for (UINT32 i = 0; i < count; i++) activates[i]->Release();
    CoTaskMemFree(activates);
    if (FAILED(hr) || !mft_) return false;

    // Set input type: H.264/HEVC
    IMFMediaType* mt = nullptr;
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, subtype);
    mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = mft_->SetInputType(0, mt, 0);
    mt->Release();
    if (FAILED(hr)) { mft_->Release(); mft_ = nullptr; return false; }

    // Set output type: NV12 (native GPU format)
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = mft_->SetOutputType(0, mt, 0);
    mt->Release();
    if (FAILED(hr)) { mft_->Release(); mft_ = nullptr; return false; }

    mft_->GetOutputStreamInfo(0, &output_info_);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    frame_w_ = w; frame_h_ = h;
    initialized_ = true;
    active_codec_ = codec;
    SPDLOG_INFO("MfH264Decoder: {} initialized (hw={})", name(), hw_accel_);
    return true;
}

bool MfH264Decoder::decode(Codec codec, bool is_keyframe,
                            const uint8_t* data, size_t size,
                            uint64_t frame_id, uint64_t timestamp_ms,
                            uint32_t width, uint32_t height) {
    if (!callback_) return false;

    // Codec::Unknown → LZ4 data, pass through to software path
    if (codec == Codec::Unknown) {
        DecodedFrame frame;
        frame.frame_id = frame_id;
        frame.width = width;
        frame.height = height;
        frame.timestamp_ms = timestamp_ms;
        frame.rgba_pixels.assign(data, data + size);
        callback_(std::move(frame));
        return true;
    }

    // For AV1: warn and fall through to passthrough
    if (codec == Codec::AV1) {
        SPDLOG_WARN("MfH264Decoder: AV1 not supported by Media Foundation.");
        DecodedFrame frame;
        frame.frame_id = frame_id;
        frame.width = width;
        frame.height = height;
        frame.timestamp_ms = timestamp_ms;
        frame.rgba_pixels.assign(data, data + size);
        callback_(std::move(frame));
        return true;
    }

    // Re-init if codec changed
    if (codec != active_codec_) {
        init_mft(codec, width, height);
    }

    // Submit compressed sample
    IMFSample* sample = nullptr;
    MFCreateSample(&sample);

    IMFMediaBuffer* buf = nullptr;
    MFCreateMemoryBuffer((DWORD)size, &buf);
    BYTE* buf_data = nullptr;
    buf->Lock(&buf_data, nullptr, nullptr);
    std::memcpy(buf_data, data, size);
    buf->Unlock();
    sample->AddBuffer(buf);
    buf->Release();

    if (is_keyframe) {
        sample->SetUINT32(MFSampleExtension_CleanPoint, 1);
    }

    HRESULT hr = mft_->ProcessInput(0, sample, 0);
    sample->Release();

    if (FAILED(hr) && hr != MF_E_NOTACCEPTING) {
        return false;
    }

    // Drain output
    current_frame_id_ = frame_id;
    return process_output();
}

bool MfH264Decoder::process_output() {
    while (true) {
        DWORD status = 0;
        MFT_OUTPUT_DATA_BUFFER output{};
        output.dwStreamID = 0;
        output.pSample = nullptr;
        output.dwStatus = 0;

        // Allocate output sample
        DWORD out_size = (output_info_.cbSize > 0) ? output_info_.cbSize : (frame_w_ * frame_h_ * 3 / 2);
        IMFSample* out_sample = nullptr;
        MFCreateSample(&out_sample);

        IMFMediaBuffer* out_buf = nullptr;
        MFCreateMemoryBuffer(out_size, &out_buf);
        out_buf->SetCurrentLength(out_size);
        out_sample->AddBuffer(out_buf);
        out_buf->Release();
        output.pSample = out_sample;

        HRESULT hr = mft_->ProcessOutput(0, 1, &output, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            out_sample->Release();
            return true; // Need more data
        }

        if (FAILED(hr)) {
            out_sample->Release();
            return false;
        }

        // Get NV12 data from output sample
        IMFMediaBuffer* out_buf2 = nullptr;
        out_sample->GetBufferByIndex(0, &out_buf2);

        BYTE* out_data = nullptr;
        DWORD out_len = 0;
        out_buf2->Lock(&out_data, nullptr, &out_len);

        if (out_data && out_len > 0) {
            int w = (int)frame_w_, h = (int)frame_h_;
            int y_size = w * h;
            auto y_plane = out_data;
            auto uv_plane = out_data + y_size;

            // NV12 to RGBA (NV12 = Y plane + interleaved UV)
            std::vector<uint8_t> rgba;
            rgba.resize(static_cast<size_t>(w) * h * 4);
            for (int row = 0; row < h; row++) {
                for (int col = 0; col < w; col++) {
                    int yi = y_plane[row * w + col];
                    int ui = uv_plane[(row / 2) * w + (col / 2) * 2 + 0] - 128;
                    int vi = uv_plane[(row / 2) * w + (col / 2) * 2 + 1] - 128;
                    int r = yi + (int)(1.402f * vi);
                    int g = yi - (int)(0.344f * ui) - (int)(0.714f * vi);
                    int b = yi + (int)(1.772f * ui);
                    auto clamp = [](int v) { return (uint8_t)std::max(0, std::min(255, v)); };
                    int idx = (row * w + col) * 4;
                    rgba[idx + 0] = clamp(r);
                    rgba[idx + 1] = clamp(g);
                    rgba[idx + 2] = clamp(b);
                    rgba[idx + 3] = 255;
                }
            }

            out_buf2->Unlock();

            DecodedFrame frame;
            frame.frame_id = current_frame_id_;
            frame.width = (uint32_t)w;
            frame.height = (uint32_t)h;
            frame.timestamp_ms = 0;
            frame.rgba_pixels = std::move(rgba);
            callback_(std::move(frame));

            create_display_window(frame.width, frame.height);
            if (g_disp_created && !frame.rgba_pixels.empty())
                update_display(frame.rgba_pixels.data(), frame.width, frame.height);
        } else {
            out_buf2->Unlock();
        }

        out_buf2->Release();
        out_sample->Release();
    }
}

#endif // _WIN32

// ==========================================================================
// SoftwareDecoder — LZ4 decompression + JPEG passthrough
// ==========================================================================
class SoftwareDecoder : public VideoDecoder {
public:
    bool init() override {
        SPDLOG_INFO("VideoDecoder: software fallback initialized");
        return true;
    }

    void shutdown() override {
        destroy_display();
        SPDLOG_INFO("VideoDecoder: shut down");
    }

    std::string name() const override { return "Software (LZ4/passthrough)"; }
    bool hardware_accelerated() const override { return false; }

    bool decode(Codec codec, bool is_keyframe,
                const uint8_t* data, size_t size,
                uint64_t frame_id, uint64_t timestamp_ms,
                uint32_t width, uint32_t height) override
    {
        (void)is_keyframe;
        if (!callback_ || !data || size == 0) return false;

        // Detect JPEG by magic bytes (0xFF 0xD8 0xFF)
        bool is_jpeg = (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF);

        if (codec == Codec::Unknown && !is_jpeg) {
            // LZ4-compressed data (lossless mode)
            if (size < 8) return false;
            uint64_t uncomp_size = 0;
            std::memcpy(&uncomp_size, data, 8);
            if (uncomp_size == 0 || uncomp_size > 64ULL * 1024 * 1024) return false;

            std::vector<uint8_t> rgba(static_cast<size_t>(uncomp_size));
            int ret = LZ4_decompress_safe(
                reinterpret_cast<const char*>(data + 8),
                reinterpret_cast<char*>(rgba.data()),
                static_cast<int>(size - 8),
                static_cast<int>(uncomp_size));
            if (ret < 0) {
                SPDLOG_WARN("VideoDecoder: LZ4 decompress failed ({})", ret);
                return false;
            }

            DecodedFrame frame;
            frame.frame_id = frame_id;
            frame.width = width;
            frame.height = height;
            frame.timestamp_ms = timestamp_ms;
            frame.rgba_pixels = std::move(rgba);
            callback_(std::move(frame));
            create_display_window(width, height);
            if (g_disp_created && !frame.rgba_pixels.empty())
                update_display(frame.rgba_pixels.data(), width, height);
            return true;
        }

        // JPEG data (adaptive compressor quality modes)
        if (is_jpeg) {
            tjhandle tj = tjInitDecompress();
            if (tj) {
                int jpeg_w = 0, jpeg_h = 0, subsamp = 0;
                if (tjDecompressHeader2(tj, const_cast<unsigned char*>(data),
                                        (unsigned long)size, &jpeg_w, &jpeg_h, &subsamp) == 0) {
                    std::vector<uint8_t> rgba(static_cast<size_t>(jpeg_w) * jpeg_h * 4);
                    if (tjDecompress2(tj, data, size, rgba.data(),
                                      jpeg_w, 0, jpeg_h, TJPF_RGBA, 0) == 0) {
                        DecodedFrame frame;
                        frame.frame_id = frame_id;
                        frame.width = (uint32_t)jpeg_w;
                        frame.height = (uint32_t)jpeg_h;
                        frame.timestamp_ms = timestamp_ms;
                        frame.rgba_pixels = std::move(rgba);
                        callback_(std::move(frame));
                        create_display_window(frame.width, frame.height);
                        if (g_disp_created && !frame.rgba_pixels.empty())
                            update_display(frame.rgba_pixels.data(), frame.width, frame.height);
                    }
                }
                tjDestroy(tj);
            }
            return true;
        }

        // H.264 / HEVC / AV1 — not implemented in software fallback
        std::string cn = (codec == Codec::H264) ? "H.264" :
                         (codec == Codec::HEVC) ? "H.265/HEVC" :
                         (codec == Codec::AV1)  ? "AV1" : "Unknown";

        SPDLOG_WARN("VideoDecoder: {} hardware decoding not available on guest. "
                    "Install FFmpeg or use Windows Media Foundation.", cn);

        if (codec == Codec::AV1) {
            SPDLOG_WARN("AV1 decode requires GPU support (RTX 30/40, Intel Arc, AMD RX 6000+) "
                        "or dav1d/FFmpeg software decoder.");
        }

        // Passthrough (corrupted but non-crashing)
        DecodedFrame frame;
        frame.frame_id = frame_id;
        frame.width = width;
        frame.height = height;
        frame.timestamp_ms = timestamp_ms;
        frame.rgba_pixels.assign(data, data + size);
        callback_(std::move(frame));
        create_display_window(width, height);
        if (g_disp_created && !frame.rgba_pixels.empty())
            update_display(frame.rgba_pixels.data(), width, height);
        return true;
    }
};

// ==========================================================================
// Factory: pick best available decoder
// ==========================================================================
VideoDecoder* create_decoder() {
#if defined(OMNIGPU_USE_FFMPEG)
    auto* ff = new FFmpegVideoDecoder();
    if (ff->init()) {
        SPDLOG_INFO("VideoDecoder: using FFmpeg (H.264/H.265/AV1 + possible HW)");
        return ff;
    }
    delete ff;
    SPDLOG_WARN("VideoDecoder: FFmpeg not available");
#endif

#ifdef _WIN32
    auto* mf = new MfH264Decoder();
    if (mf->init()) {
        SPDLOG_INFO("VideoDecoder: using Media Foundation (H.264/H.265 HW)");
        return mf;
    }
    delete mf;
    SPDLOG_WARN("VideoDecoder: Media Foundation not available, falling back to software");
#endif

    return new SoftwareDecoder();
}

} // namespace omnigpu::video
