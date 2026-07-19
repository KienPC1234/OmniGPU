#include "ffmpeg_encoder.h"
#include <spdlog/spdlog.h>

#if defined(OMNIGPU_USE_FFMPEG)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <cstring>
#include <string>
#include <vector>

namespace omnigpu {

// Returns true for pixel formats that require HW-specific frame allocation
// (VAAPI, QSV, D3D11, CUDA, Vulkan). Encoders using these formats need
// hw_frames_ctx setup which we don't implement — skip them in auto-probe.
static bool is_hw_pix_fmt(AVPixelFormat fmt) {
    switch (fmt) {
    case AV_PIX_FMT_VAAPI:
    case AV_PIX_FMT_QSV:
    case AV_PIX_FMT_D3D11:
    case AV_PIX_FMT_D3D11VA_VLD:
    case AV_PIX_FMT_CUDA:
    case AV_PIX_FMT_VULKAN:
    case AV_PIX_FMT_DRM_PRIME:
        return true;
    default:
        return false;
    }
}

static const char* hw_encoders_h264[] = {
    "h264_nvenc", "h264_amf", "h264_vaapi", "h264_qsv", nullptr
};
static const char* sw_encoders_h264[] = {
    "libx264", "h264", nullptr
};

static const char* hw_encoders_hevc[] = {
    "hevc_nvenc", "hevc_amf", "hevc_vaapi", "hevc_qsv", nullptr
};
static const char* sw_encoders_hevc[] = {
    "libx265", "hevc", nullptr
};

static const char* hw_encoders_av1[] = {
    "av1_vaapi", "av1_qsv", "av1_nvenc", nullptr
};
static const char* sw_encoders_av1[] = {
    "libsvtav1", "libaom-av1", nullptr
};

struct FFmpegEncoder::Impl {
    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    AVPacket* pkt = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;
    int fps = 30;
    int bitrateKbps = 4000;
    bool initialized = false;
    int64_t frameIdx = 0;

    std::string encoderName = "auto";
    std::string activeEncoderName;
    std::string optPreset = "fast";
    std::string optTuning = "low_latency";
    int optGopLength = 0;

    bool open_encoder(const char* enc_name, AVCodecID codec_id) {
        const AVCodec* codec = avcodec_find_encoder_by_name(enc_name);
        if (!codec) return false;

        if (ctx) avcodec_free_context(&ctx);
        ctx = avcodec_alloc_context3(codec);
        if (!ctx) return false;

        ctx->width = static_cast<int>(width);
        ctx->height = static_cast<int>(height);
        ctx->time_base = AVRational{1, fps};
        ctx->framerate = AVRational{fps, 1};
        ctx->bit_rate = static_cast<int64_t>(bitrateKbps) * 1000;
        ctx->max_b_frames = 0;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

        AVPixelFormat selected_fmt = AV_PIX_FMT_NONE;
    #if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)
        const enum AVPixelFormat *pix_fmts = nullptr;
        int ret = avcodec_get_supported_config(nullptr, codec,
            AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&pix_fmts, nullptr);
        if (ret >= 0 && pix_fmts) {
            for (int i = 0; pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                if (!is_hw_pix_fmt(pix_fmts[i])) {
                    selected_fmt = pix_fmts[i];
                    break;
                }
            }
        }
    #else
        if (codec->pix_fmts) {
            for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                if (!is_hw_pix_fmt(codec->pix_fmts[i])) {
                    selected_fmt = codec->pix_fmts[i];
                    break;
                }
            }
        }
    #endif
        if (selected_fmt != AV_PIX_FMT_NONE) {
            ctx->pix_fmt = selected_fmt;
        } else {
            ctx->pix_fmt = AV_PIX_FMT_NV12;
        }

        ctx->gop_size = optGopLength;

        std::string enc_name_str = enc_name;
        for (auto& c : enc_name_str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (enc_name_str.find("nvenc") != std::string::npos) {
            std::string nv_preset = "p1"; 
            if (optPreset == "slow" || optPreset == "hq" || optPreset == "quality") nv_preset = "p6";
            else if (optPreset == "medium" || optPreset == "balanced") nv_preset = "p4";
            else if (optPreset == "fast" || optPreset == "hp" || optPreset == "speed") nv_preset = "p2";
            
            std::string nv_tune = "ull"; 
            if (optTuning == "hq" || optTuning == "quality") nv_tune = "hq";
            else if (optTuning == "ll" || optTuning == "low_latency" || optTuning == "zerolatency") nv_tune = "ll";

            av_opt_set(ctx->priv_data, "preset", nv_preset.c_str(), AV_OPT_SEARCH_CHILDREN);
            av_opt_set(ctx->priv_data, "tune", nv_tune.c_str(), AV_OPT_SEARCH_CHILDREN);
        } else if (enc_name_str.find("amf") != std::string::npos) {
            std::string amf_preset = "speed";
            if (optPreset == "slow" || optPreset == "quality") amf_preset = "quality";
            else if (optPreset == "medium" || optPreset == "balanced") amf_preset = "balanced";

            av_opt_set(ctx->priv_data, "preset", amf_preset.c_str(), AV_OPT_SEARCH_CHILDREN);
            av_opt_set(ctx->priv_data, "usage", "lowlatency", AV_OPT_SEARCH_CHILDREN);
        } else if (enc_name_str.find("qsv") != std::string::npos) {
            std::string qsv_preset = "veryfast";
            if (optPreset == "slow" || optPreset == "quality") qsv_preset = "slow";
            else if (optPreset == "medium") qsv_preset = "medium";
            else if (optPreset == "fast" || optPreset == "speed") qsv_preset = "fast";

            av_opt_set(ctx->priv_data, "preset", qsv_preset.c_str(), AV_OPT_SEARCH_CHILDREN);
        } else {
            av_opt_set(ctx->priv_data, "preset", optPreset.c_str(), AV_OPT_SEARCH_CHILDREN);
            av_opt_set(ctx->priv_data, "tune", optTuning.c_str(), AV_OPT_SEARCH_CHILDREN);
        }

        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return false;
        }

        // Skip encoders requiring HW-specific frame allocation
        if (is_hw_pix_fmt(ctx->pix_fmt)) {
            avcodec_free_context(&ctx);
            return false;
        }

        activeEncoderName = enc_name;
        return true;
    }

    bool init_encoder(VideoCodec codec) {
        AVCodecID codec_id = AV_CODEC_ID_NONE;
        switch (codec) {
        case VideoCodec::H264: codec_id = AV_CODEC_ID_H264; break;
        case VideoCodec::HEVC: codec_id = AV_CODEC_ID_HEVC; break;
        case VideoCodec::AV1:  codec_id = AV_CODEC_ID_AV1;  break;
        }

        if (encoderName != "auto") {
            if (open_encoder(encoderName.c_str(), codec_id)) {
                return true;
            }
            SPDLOG_WARN("FFmpeg: requested encoder '{}' not available, probing...",
                        encoderName);
        }

        if (codec_id == AV_CODEC_ID_H264) {
            for (int i = 0; hw_encoders_h264[i]; ++i)
                if (open_encoder(hw_encoders_h264[i], codec_id)) return true;
            for (int i = 0; sw_encoders_h264[i]; ++i)
                if (open_encoder(sw_encoders_h264[i], codec_id)) return true;
        } else if (codec_id == AV_CODEC_ID_HEVC) {
            for (int i = 0; hw_encoders_hevc[i]; ++i)
                if (open_encoder(hw_encoders_hevc[i], codec_id)) return true;
            for (int i = 0; sw_encoders_hevc[i]; ++i)
                if (open_encoder(sw_encoders_hevc[i], codec_id)) return true;
        } else if (codec_id == AV_CODEC_ID_AV1) {
            for (int i = 0; hw_encoders_av1[i]; ++i)
                if (open_encoder(hw_encoders_av1[i], codec_id)) return true;
            for (int i = 0; sw_encoders_av1[i]; ++i)
                if (open_encoder(sw_encoders_av1[i], codec_id)) return true;
        }

        return false;
    }

    ~Impl() {
        if (ctx) avcodec_free_context(&ctx);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (sws) { sws_freeContext(sws); sws = nullptr; }
    }
};

FFmpegEncoder::FFmpegEncoder() : impl_(std::make_unique<Impl>()) {}
FFmpegEncoder::~FFmpegEncoder() { shutdown(); }

void FFmpegEncoder::set_encoder_options(const std::string& preset, const std::string& tuning, int gop_length) {
    impl_->optPreset = preset;
    impl_->optTuning = tuning;
    impl_->optGopLength = gop_length;
}

void FFmpegEncoder::set_encoder_name(const std::string& name) {
    impl_->encoderName = name;
}

bool FFmpegEncoder::available() const {
    return true;
}

std::string FFmpegEncoder::name() const {
    return "FFmpeg/" + impl_->activeEncoderName;
}

bool FFmpegEncoder::init(VideoCodec codec, uint32_t width, uint32_t height,
                          int fps, int bitrateKbps) {
    if (impl_->initialized) shutdown();

    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;
    impl_->bitrateKbps = bitrateKbps;

    if (!impl_->init_encoder(codec)) {
        SPDLOG_ERROR("FFmpeg: no encoder found for codec={}", static_cast<int>(codec));
        return false;
    }

    if (impl_->frame) av_frame_free(&impl_->frame);
    impl_->frame = av_frame_alloc();
    if (!impl_->frame) {
        shutdown();
        return false;
    }

    impl_->frame->width = static_cast<int>(width);
    impl_->frame->height = static_cast<int>(height);
    impl_->frame->format = impl_->ctx->pix_fmt;

    if (av_frame_get_buffer(impl_->frame, 0) < 0) {
        SPDLOG_ERROR("FFmpeg: failed to allocate frame buffer");
        shutdown();
        return false;
    }

    if (impl_->sws) { sws_freeContext(impl_->sws); impl_->sws = nullptr; }
    impl_->sws = sws_getContext(
        static_cast<int>(width), static_cast<int>(height), AV_PIX_FMT_RGBA,
        static_cast<int>(width), static_cast<int>(height), impl_->ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!impl_->sws) {
        SPDLOG_ERROR("FFmpeg: failed to create SwsContext");
        shutdown();
        return false;
    }

    if (impl_->pkt) av_packet_free(&impl_->pkt);
    impl_->pkt = av_packet_alloc();
    if (!impl_->pkt) {
        shutdown();
        return false;
    }

    impl_->initialized = true;
    SPDLOG_INFO("FFmpeg: encoder '{}' initialized ({}x{} @ {}fps, {} kbps, codec={})",
                impl_->activeEncoderName, width, height, fps, bitrateKbps,
                codec == VideoCodec::H264 ? "H264" :
                codec == VideoCodec::HEVC ? "HEVC" : "AV1");
    return true;
}

void FFmpegEncoder::shutdown() {
    if (!impl_->initialized) return;

    if (impl_->ctx) {
        avcodec_send_frame(impl_->ctx, nullptr);
        AVPacket* flushPkt = av_packet_alloc();
        while (avcodec_receive_packet(impl_->ctx, flushPkt) >= 0) {
            av_packet_unref(flushPkt);
        }
        av_packet_free(&flushPkt);
        avcodec_free_context(&impl_->ctx);
    }
    if (impl_->frame) av_frame_free(&impl_->frame);
    if (impl_->pkt) av_packet_free(&impl_->pkt);
    if (impl_->sws) { sws_freeContext(impl_->sws); impl_->sws = nullptr; }

    impl_->initialized = false;
    SPDLOG_INFO("FFmpeg: encoder shut down");
}

bool FFmpegEncoder::encode(const std::vector<uint8_t>& rgba,
                            std::vector<EncodedPacket>& packets) {
    if (!impl_->initialized || !impl_->ctx || !impl_->frame || !impl_->sws) {
        return false;
    }

    const uint8_t* srcSlice[1] = { rgba.data() };
    int srcStride[1] = { static_cast<int>(impl_->width) * 4 };

    if (sws_scale(impl_->sws, srcSlice, srcStride, 0,
                  static_cast<int>(impl_->height),
                  impl_->frame->data, impl_->frame->linesize) < 0) {
        SPDLOG_ERROR("FFmpeg: sws_scale failed");
        return false;
    }

    impl_->frame->pts = impl_->frameIdx++;

    int ret = avcodec_send_frame(impl_->ctx, impl_->frame);
    if (ret < 0) {
        char errbuf[256] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        SPDLOG_ERROR("FFmpeg: avcodec_send_frame failed: {} (code: {})", errbuf, ret);
        return false;
    }

    while (true) {
        ret = avcodec_receive_packet(impl_->ctx, impl_->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            char errbuf[256] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            SPDLOG_ERROR("FFmpeg: avcodec_receive_packet failed: {} (code: {})", errbuf, ret);
            return false;
        }

        EncodedPacket packet;
        packet.data.assign(impl_->pkt->data, impl_->pkt->data + impl_->pkt->size);
        packet.isKeyframe = (impl_->pkt->flags & AV_PKT_FLAG_KEY) != 0;
        packet.pts = impl_->pkt->pts;
        packets.push_back(std::move(packet));

        av_packet_unref(impl_->pkt);
    }

    return true;
}

bool FFmpegEncoder::flush(std::vector<EncodedPacket>& packets) {
    if (!impl_->initialized || !impl_->ctx) return false;

    avcodec_send_frame(impl_->ctx, nullptr);

    while (true) {
        int ret = avcodec_receive_packet(impl_->ctx, impl_->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        EncodedPacket packet;
        packet.data.assign(impl_->pkt->data, impl_->pkt->data + impl_->pkt->size);
        packet.isKeyframe = (impl_->pkt->flags & AV_PKT_FLAG_KEY) != 0;
        packet.pts = impl_->pkt->pts;
        packets.push_back(std::move(packet));

        av_packet_unref(impl_->pkt);
    }

    return true;
}

} // namespace omnigpu

#else // !OMNIGPU_USE_FFMPEG

namespace omnigpu {

struct FFmpegEncoder::Impl {};

FFmpegEncoder::FFmpegEncoder() : impl_(std::make_unique<Impl>()) {}
FFmpegEncoder::~FFmpegEncoder() = default;

void FFmpegEncoder::set_encoder_name(const std::string&) {}

bool FFmpegEncoder::init(VideoCodec, uint32_t, uint32_t, int, int) {
    return false;
}

bool FFmpegEncoder::encode(const std::vector<uint8_t>&,
                            std::vector<EncodedPacket>&) {
    return false;
}

bool FFmpegEncoder::flush(std::vector<EncodedPacket>&) {
    return false;
}

void FFmpegEncoder::shutdown() {}

bool FFmpegEncoder::available() const {
    return false;
}

std::string FFmpegEncoder::name() const {
    return "FFmpeg (unavailable)";
}

} // namespace omnigpu

#endif // OMNIGPU_USE_FFMPEG
