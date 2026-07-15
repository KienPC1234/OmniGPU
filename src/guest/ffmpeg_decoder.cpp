#include "ffmpeg_decoder.h"

#if defined(OMNIGPU_USE_FFMPEG)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu::video {

struct FFmpegVideoDecoder::Impl {
    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sw_frame = nullptr;
    SwsContext* sws = nullptr;
    uint8_t* rgba_buf = nullptr;
    int rgba_buf_size = 0;
    AVCodecID av_codec_id = AV_CODEC_ID_NONE;
    bool hw_enabled = false;
    AVBufferRef* hw_device_ctx = nullptr;
    AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;
};

FFmpegVideoDecoder::FFmpegVideoDecoder()
    : impl_(std::make_unique<Impl>()) {}

FFmpegVideoDecoder::~FFmpegVideoDecoder() { shutdown(); }

// Try to init HW device for a given device type
static bool try_init_hw(AVHWDeviceType type, AVBufferRef** device_ctx) {
    // Only try supported types
    switch (type) {
#ifdef _WIN32
    case AV_HWDEVICE_TYPE_D3D11VA:
    case AV_HWDEVICE_TYPE_DXVA2:
        break;
#else
    case AV_HWDEVICE_TYPE_VAAPI:
    case AV_HWDEVICE_TYPE_VDPAU:
    case AV_HWDEVICE_TYPE_VULKAN:
        break;
#endif
    default:
        return false;
    }
    return av_hwdevice_ctx_create(device_ctx, type, nullptr, nullptr, 0) == 0;
}

bool FFmpegVideoDecoder::init() {
    // Probe available HW decoders
    AVHWDeviceType types[] = {
#ifdef _WIN32
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2,
#else
        AV_HWDEVICE_TYPE_VAAPI,
        AV_HWDEVICE_TYPE_VDPAU,
        AV_HWDEVICE_TYPE_VULKAN,
#endif
        AV_HWDEVICE_TYPE_NONE
    };
    for (int i = 0; types[i] != AV_HWDEVICE_TYPE_NONE; i++) {
        if (try_init_hw(types[i], &impl_->hw_device_ctx)) {
            impl_->hw_type = types[i];
            impl_->hw_enabled = true;
            SPDLOG_INFO("FFmpeg: HW acceleration available (type={})",
                        av_hwdevice_get_type_name(types[i]));
            break;
        }
    }

    SPDLOG_INFO("FFmpegVideoDecoder: initialized (avcodec {}, hw={})",
                avcodec_version(), impl_->hw_enabled);
    return true;
}

void FFmpegVideoDecoder::shutdown() {
    if (impl_->rgba_buf) { av_free(impl_->rgba_buf); impl_->rgba_buf = nullptr; }
    if (impl_->sws) { sws_freeContext(impl_->sws); impl_->sws = nullptr; }
    if (impl_->frame) { av_frame_free(&impl_->frame); }
    if (impl_->sw_frame) { av_frame_free(&impl_->sw_frame); }
    if (impl_->ctx) {
        avcodec_close(impl_->ctx);
        avcodec_free_context(&impl_->ctx);
    }
    if (impl_->hw_device_ctx) { av_buffer_unref(&impl_->hw_device_ctx); }
    impl_->rgba_buf_size = 0;
    SPDLOG_INFO("FFmpegVideoDecoder: shut down");
}

bool FFmpegVideoDecoder::hardware_accelerated() const {
    return impl_->hw_enabled;
}

std::string FFmpegVideoDecoder::name() const {
    if (!impl_->hw_enabled) return "FFmpeg (software)";
    return "FFmpeg (" + std::string(av_hwdevice_get_type_name(impl_->hw_type)) + ")";
}

// Map codec + HW type → decoder name
static const char* get_hw_decoder_name(AVCodecID codec_id, AVHWDeviceType hw_type) {
#ifdef _WIN32
    if (hw_type == AV_HWDEVICE_TYPE_D3D11VA) {
        switch (codec_id) {
            case AV_CODEC_ID_H264: return "h264_d3d11va";
            case AV_CODEC_ID_HEVC: return "hevc_d3d11va";
            case AV_CODEC_ID_AV1:  return "av1_d3d11va";
            default: return nullptr;
        }
    }
    if (hw_type == AV_HWDEVICE_TYPE_DXVA2) {
        switch (codec_id) {
            case AV_CODEC_ID_H264: return "h264_dxva2";
            case AV_CODEC_ID_HEVC: return "hevc_dxva2";
            default: return nullptr;
        }
    }
#else
    if (hw_type == AV_HWDEVICE_TYPE_VAAPI) {
        switch (codec_id) {
            case AV_CODEC_ID_H264: return "h264_vaapi";
            case AV_CODEC_ID_HEVC: return "hevc_vaapi";
            case AV_CODEC_ID_AV1:  return "av1_vaapi";
            default: return nullptr;
        }
    }
    if (hw_type == AV_HWDEVICE_TYPE_VDPAU) {
        switch (codec_id) {
            case AV_CODEC_ID_H264: return "h264_vdpau";
            case AV_CODEC_ID_HEVC: return "hevc_vdpau";
            default: return nullptr;
        }
    }
    if (hw_type == AV_HWDEVICE_TYPE_VULKAN) {
        switch (codec_id) {
            case AV_CODEC_ID_H264: return "h264_vulkan";
            case AV_CODEC_ID_HEVC: return "hevc_vulkan";
            case AV_CODEC_ID_AV1:  return "av1_vulkan";
            default: return nullptr;
        }
    }
#endif
    return nullptr;
}

bool FFmpegVideoDecoder::decode(Codec codec, bool is_keyframe,
                                 const uint8_t* data, size_t size,
                                 uint64_t frame_id, uint64_t timestamp_ms,
                                 uint32_t width, uint32_t height) {
    if (!callback_ || !data || size == 0) return false;

    AVCodecID new_id = AV_CODEC_ID_NONE;
    switch (codec) {
    case Codec::H264: new_id = AV_CODEC_ID_H264; break;
    case Codec::HEVC: new_id = AV_CODEC_ID_HEVC; break;
    case Codec::AV1:  new_id = AV_CODEC_ID_AV1;  break;
    default: SPDLOG_WARN("FFmpeg: unsupported codec {}", (int)codec); return false;
    }

    // Re-init on codec change
    if (!impl_->ctx || new_id != impl_->av_codec_id) {
        if (impl_->ctx) {
            avcodec_close(impl_->ctx);
            avcodec_free_context(&impl_->ctx);
        }
        impl_->av_codec_id = new_id;

        const AVCodec* avc = nullptr;
        // Try HW decoder matching our codec + HW type
        if (impl_->hw_enabled) {
            const char* hw_name = get_hw_decoder_name(new_id, impl_->hw_type);
            if (hw_name) {
                avc = avcodec_find_decoder_by_name(hw_name);
                if (avc) SPDLOG_INFO("FFmpeg: using HW decoder '{}'", hw_name);
            }
        }
        // Fall back to software
        if (!avc) {
            avc = avcodec_find_decoder(new_id);
            if (!avc) {
                SPDLOG_ERROR("FFmpeg: no decoder for codec {}", (int)codec);
                return false;
            }
        }

        impl_->ctx = avcodec_alloc_context3(avc);
        if (!impl_->ctx) return false;

        // Attach HW device context if using HW decoder
        if (impl_->hw_enabled && impl_->hw_device_ctx) {
            impl_->ctx->hw_device_ctx = av_buffer_ref(impl_->hw_device_ctx);
        }

        // Multi-threaded for software decode
        if (codec == Codec::AV1 || codec == Codec::HEVC) {
            impl_->ctx->thread_count = 4;
            impl_->ctx->thread_type = FF_THREAD_FRAME;
        }

        if (avcodec_open2(impl_->ctx, avc, nullptr) < 0) {
            avcodec_free_context(&impl_->ctx);
            impl_->ctx = nullptr;
            SPDLOG_ERROR("FFmpeg: failed to open decoder");
            return false;
        }

        std::string cn = (codec == Codec::H264) ? "H.264" :
                         (codec == Codec::HEVC) ? "H.265" : "AV1";
        SPDLOG_INFO("FFmpeg: {} decoder ready (hw={})", cn, impl_->hw_enabled);
    }

    // Send packet
    AVPacket* pkt = av_packet_alloc();
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    pkt->flags = is_keyframe ? AV_PKT_FLAG_KEY : 0;

    int ret = avcodec_send_packet(impl_->ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        if (ret == AVERROR_INVALIDDATA) return true;
        return false;
    }

    if (!impl_->frame) impl_->frame = av_frame_alloc();

    while (true) {
        ret = avcodec_receive_frame(impl_->ctx, impl_->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        AVFrame* src = impl_->frame;

        // Transfer HW frame to CPU if needed
        if (impl_->frame->format == AV_PIX_FMT_D3D11VA_VLD ||
            impl_->frame->format == AV_PIX_FMT_DXVA2_VLD ||
            impl_->frame->format == AV_PIX_FMT_VAAPI ||
            impl_->frame->format == AV_PIX_FMT_VDPAU ||
            impl_->frame->format == AV_PIX_FMT_VULKAN) {

            if (!impl_->sw_frame) impl_->sw_frame = av_frame_alloc();
            if (av_hwframe_transfer_data(impl_->sw_frame, impl_->frame, 0) == 0) {
                src = impl_->sw_frame;
            }
        }

        // Convert to RGBA
        int w = src->width;
        int h = src->height;

        impl_->sws = sws_getCachedContext(impl_->sws,
            w, h, src->format,
            w, h, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        int rgba_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
        if (rgba_size > impl_->rgba_buf_size) {
            impl_->rgba_buf = (uint8_t*)av_realloc(impl_->rgba_buf, (size_t)rgba_size);
            impl_->rgba_buf_size = rgba_size;
        }

        uint8_t* dst[] = { impl_->rgba_buf, nullptr, nullptr, nullptr };
        int dst_stride[] = { w * 4, 0, 0, 0 };
        sws_scale(impl_->sws, src->data, src->linesize, 0, h, dst, dst_stride);

        DecodedFrame frame;
        frame.frame_id = frame_id;
        frame.width = (uint32_t)w;
        frame.height = (uint32_t)h;
        frame.timestamp_ms = timestamp_ms;
        frame.rgba_pixels.assign(impl_->rgba_buf, impl_->rgba_buf + (size_t)rgba_size);
        callback_(std::move(frame));

        av_frame_unref(impl_->frame);
        if (impl_->sw_frame) av_frame_unref(impl_->sw_frame);
    }

    return true;
}

} // namespace omnigpu::video

#endif // OMNIGPU_USE_FFMPEG
