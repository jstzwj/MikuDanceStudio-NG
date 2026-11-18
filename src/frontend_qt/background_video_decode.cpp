#include "background_video.hpp"
#include "media_runtime.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace mikudancestudio::qt {
namespace {
bool Fail(std::string& reason, const char* text) {
    reason = text;
    return false;
}
constexpr AVRational Microseconds{1, 1000000};
bool Size(int width, int height) {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
           std::int64_t(width) * height <= 16777216;
}
}
struct VideoDecoder::Impl {
    MediaRuntime& api = VideoRuntime();
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* scale = nullptr;
    media::VideoInfo info;
    std::filesystem::path path;
    AVStream* stream = nullptr;
    std::int64_t origin = 0;
    int index = -1, turns = 0, scaledWidth = 0;
    bool draining = false, eof = false;
    bool avi = false, ordinalThirty = false;
    media::VideoFrame current, next;
    media::VideoProceed proceed;
    std::chrono::steady_clock::time_point deadline;
    ~Impl() {
        if (scale)
            api.sws_freeContext(scale);
        if (frame)
            api.av_frame_free(&frame);
        if (packet)
            api.av_packet_free(&packet);
        if (codec)
            api.avcodec_free_context(&codec);
        if (format)
            api.avformat_close_input(&format);
    }
    const char* interruption = nullptr;
    bool Interrupted() {
        if (!interruption && proceed && !proceed())
            interruption = "background-video-cancelled";
        if (!interruption && std::chrono::steady_clock::now() > deadline)
            interruption = "background-video-timeout";
        return interruption != nullptr;
    }
    static int Interrupt(void* context) { return static_cast<Impl*>(context)->Interrupted(); }
    bool Failed(std::string& reason, const char* fallback) {
        return Fail(reason, Interrupted() ? interruption : fallback);
    }
    bool Convert(media::VideoFrame& out, std::string& reason) {
        if (!Size(frame->width, frame->height) || frame->width != codec->width ||
            frame->height != codec->height)
            return Fail(reason, "background-video-dynamic-size-unsupported");
        if (frame->color_trc == AVCOL_TRC_SMPTE2084 || frame->color_trc == AVCOL_TRC_ARIB_STD_B67)
            return Fail(reason, "background-video-hdr-transfer-unsupported");
        const auto stamp = frame->best_effort_timestamp;
        if (stamp == AV_NOPTS_VALUE)
            return Fail(reason, "background-video-timestamp-missing");
        out.startUs = api.av_rescale_q(stamp - origin, stream->time_base, Microseconds);
        const auto duration =
            frame->duration > 0
                ? api.av_rescale_q(frame->duration, stream->time_base, Microseconds)
                : (stream->avg_frame_rate.num > 0
                       ? api.av_rescale_q(1, av_inv_q(stream->avg_frame_rate), Microseconds)
                       : 33333);
        out.endUs = out.startUs + std::max<std::int64_t>(1, duration);
        const auto formatId = static_cast<AVPixelFormat>(frame->format);
        scale = api.sws_getCachedContext(
            scale, frame->width, frame->height, formatId, scaledWidth, frame->height,
            AV_PIX_FMT_RGBA, SWS_BILINEAR | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
        if (!scale)
            return Fail(reason, "background-video-color-converter-failed");
        const int matrix = frame->colorspace == AVCOL_SPC_BT709        ? SWS_CS_ITU709
                           : frame->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020
                                                                       : SWS_CS_ITU601;
        const auto* coefficients = api.sws_getCoefficients(matrix);
        if (api.sws_setColorspaceDetails(scale, coefficients,
                                         frame->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0,
                                         1 << 16, 1 << 16) < 0)
            return Fail(reason, "background-video-color-matrix-failed");
        auto image = std::make_shared<rhi::RgbaImage>();
        image->width = info.width;
        image->height = info.height;
        image->pixels.resize(std::size_t(info.width) * info.height * 4);
        auto* pixels = image->pixels.data();
        std::vector<std::uint8_t> rotated;
        if (turns)
            rotated.resize(image->pixels.size());
        std::uint8_t* data[4]{turns ? rotated.data() : pixels};
        int stride[4]{scaledWidth * 4};
        if (api.sws_scale(scale, frame->data, frame->linesize, 0, frame->height, data, stride) !=
            frame->height)
            return Fail(reason, "background-video-color-conversion-failed");
        if (turns)
            for (int y = 0; y < frame->height; ++y)
                for (int x = 0; x < scaledWidth; ++x) {
                    const int dx = turns == 1   ? frame->height - 1 - y
                                   : turns == 2 ? scaledWidth - 1 - x
                                                : y;
                    const int dy = turns == 1   ? x
                                   : turns == 2 ? frame->height - 1 - y
                                                : scaledWidth - 1 - x;
                    std::memcpy(pixels + (std::size_t(dy) * info.width + dx) * 4,
                                rotated.data() + (std::size_t(y) * scaledWidth + x) * 4, 4);
                }
        // Native AVI background textures are opaque X8R8G8B8, even for alpha codecs.
        for (std::size_t n = 3; n < image->pixels.size(); n += 4)
            pixels[n] = 255;
        image->pixels.Seal();
        out.image = std::move(image);
        return true;
    }
    bool Decode(media::VideoFrame& out, std::string& reason) {
        out = {};
        for (unsigned packets = 0; packets < 100000; ++packets) {
            if (Interrupted())
                return Fail(reason, interruption);
            const auto received = api.avcodec_receive_frame(codec, frame);
            if (received == 0) {
                const bool ok = Convert(out, reason);
                api.av_frame_unref(frame);
                return ok;
            }
            if (received == AVERROR_EOF) {
                eof = true;
                return true;
            }
            if (received != AVERROR(EAGAIN))
                return Fail(reason, "background-video-decode-failed");
            if (draining)
                return Fail(reason, "background-video-drain-stalled");
            int read = 0;
            do {
                api.av_packet_unref(packet);
                if (Interrupted())
                    return Fail(reason, interruption);
                read = api.av_read_frame(format, packet);
            } while (read >= 0 && packet->stream_index != index);
            if (read == AVERROR_EOF) {
                draining = true;
                if (api.avcodec_send_packet(codec, nullptr) < 0)
                    return Fail(reason, "background-video-drain-failed");
            } else if (read < 0) {
                return Failed(reason, "background-video-packet-read-failed");
            } else {
                if (packet->size > 64 * 1024 * 1024)
                    return Fail(reason, "background-video-packet-limit");
                if (api.avcodec_send_packet(codec, packet) < 0)
                    return Fail(reason, "background-video-packet-decode-failed");
                api.av_packet_unref(packet);
            }
        }
        return Fail(reason, "background-video-decode-work-limit");
    }
};
VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() = default;
const media::VideoInfo& VideoDecoder::Info() const {
    static const media::VideoInfo empty;
    return impl_ ? impl_->info : empty;
}
bool VideoDecoder::Open(const std::filesystem::path& path, std::string& reason,
                        const media::VideoProceed& proceed) {
    impl_.reset();
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(path, fileError))
        return Fail(reason, "background-video-file-missing");
    auto p = std::make_unique<Impl>();
    p->path = path;
    if (!p->api.error.empty()) {
        reason = p->api.error;
        return false;
    }
    p->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    p->proceed = proceed;
    if (p->Interrupted())
        return Fail(reason, p->interruption);
    p->format = p->api.avformat_alloc_context();
    if (!p->format)
        return Fail(reason, "background-video-allocation-failed");
    p->format->interrupt_callback = {Impl::Interrupt, p.get()};
    p->format->probesize = 8 * 1024 * 1024;
    p->format->max_analyze_duration = 5000000;
    p->format->max_streams = 32;
    // Local files only. Do not hand a URL or a nested network playlist to a decoder.
    AVDictionary* options = nullptr;
    p->api.av_dict_set(&options, "protocol_whitelist", "file", 0);
    p->api.av_dict_set(&options, "format_whitelist", "avi,mov,matroska,webm", 0);
    const auto opened =
        p->api.avformat_open_input(&p->format, path.u8string().c_str(), nullptr, &options);
    p->api.av_dict_free(&options);
    if (opened < 0 || p->api.avformat_find_stream_info(p->format, nullptr) < 0)
        return p->Failed(reason, "background-video-open-failed");
    const AVCodec* decoder = nullptr;
    p->index = p->api.av_find_best_stream(p->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (p->index < 0 || !decoder)
        return Fail(reason, "background-video-stream-missing");
    p->stream = p->format->streams[p->index];
    p->avi = std::string(p->format->iformat->name) == "avi";
    const auto rate = p->stream->avg_frame_rate;
    p->ordinalThirty =
        p->avi && rate.num > 0 && rate.den > 0 && std::int64_t(rate.den) * 1000 / rate.num == 33;
    unsigned videoStreams = 0;
    for (unsigned n = 0; n < p->format->nb_streams; ++n)
        videoStreams += p->format->streams[n]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    if (videoStreams != 1)
        return Fail(reason, "background-video-multiple-video-streams-unsupported");
    const auto* parameters = p->stream->codecpar;
    if (!Size(parameters->width, parameters->height))
        return Fail(reason, "background-video-size-limit");
    p->codec = p->api.avcodec_alloc_context3(decoder);
    if (!p->codec || p->api.avcodec_parameters_to_context(p->codec, parameters) < 0)
        return Fail(reason, "background-video-codec-allocation-failed");
    p->codec->thread_count = 2;
    p->codec->max_pixels = 16777216;
    if (p->api.avcodec_open2(p->codec, decoder, nullptr) < 0)
        return Fail(reason, "background-video-codec-unsupported");
    p->origin = p->stream->start_time == AV_NOPTS_VALUE ? 0 : p->stream->start_time;
    p->info.durationUs =
        p->stream->duration > 0
            ? p->api.av_rescale_q(p->stream->duration, p->stream->time_base, Microseconds)
            : p->format->duration;
    if (p->info.durationUs <= 0 || p->info.durationUs > 21600000000LL)
        return Fail(reason, "background-video-duration-limit-or-missing");
    const auto sar = p->api.av_guess_sample_aspect_ratio(p->format, p->stream, nullptr);
    p->scaledWidth = sar.num > 0 && sar.den > 0
                         ? int(std::int64_t(parameters->width) * sar.num / sar.den)
                         : parameters->width;
    if (!Size(p->scaledWidth, parameters->height))
        return Fail(reason, "background-video-display-size-limit");
    const auto* rotation = p->api.av_packet_side_data_get(
        parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (rotation && rotation->size >= 9 * sizeof(std::int32_t)) {
        const auto* matrix = reinterpret_cast<const std::int32_t*>(rotation->data);
        const auto determinant = double(matrix[0]) * matrix[4] - double(matrix[1]) * matrix[3];
        if (determinant <= 0)
            return Fail(reason, "background-video-mirrored-matrix-unsupported");
        const auto angle = -p->api.av_display_rotation_get(matrix);
        if (!std::isfinite(angle) || std::abs(angle / 90 - std::round(angle / 90)) > .001)
            return Fail(reason, "background-video-nonorthogonal-rotation-unsupported");
        p->turns = (int(std::round(angle / 90)) % 4 + 4) % 4;
    }
    p->info.clockwiseRotation = p->turns * 90;
    p->info.width = p->turns & 1 ? parameters->height : p->scaledWidth;
    p->info.height = p->turns & 1 ? p->scaledWidth : parameters->height;
    p->info.color = "SDR RGBA8; declared YUV range/matrix; unspecified=BT.601 limited";
    p->packet = p->api.av_packet_alloc();
    p->frame = p->api.av_frame_alloc();
    if (!p->packet || !p->frame)
        return Fail(reason, "background-video-allocation-failed");
    impl_ = std::move(p);
    impl_->proceed = {};
    reason.clear();
    return true;
}
bool VideoDecoder::Read(double seconds, media::VideoFrame& out, const media::VideoProceed& proceed,
                        std::string& reason) {
    out = {};
    std::int64_t target;
    if (!impl_ || !media::VideoTime(seconds, target))
        return Fail(reason, "background-video-time-invalid-or-not-open");
    auto& p = *impl_;
    p.proceed = proceed;
    struct ResetProceed {
        Impl& impl;
        ~ResetProceed() { impl.proceed = {}; }
    } reset{p};
    p.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    p.interruption = nullptr;
    if (p.Interrupted())
        return Fail(reason, p.interruption);
    if (p.current.Covers(target)) {
        out = p.current;
        reason.clear();
        return true;
    }
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(p.path, fileError))
        return Fail(reason, "background-video-file-missing");
    // Sequential playback reuses two decoded frames; seek only for backwards
    // or distant movement. Decode from the keyframe, never trust seek's result.
    if (!p.current.image || target < p.current.startUs || target > p.current.endUs + 2000000) {
        // Some AVI/MP4 indexes locate by decode timestamps, ahead of the
        // presentation timestamp with reordered B pictures. Preroll from an
        // earlier keyframe, then select by decoded PTS rather than seek output.
        const auto seekUs =
            std::max<std::int64_t>(0, std::min(target, p.info.durationUs - 1) - 2000000);
        const auto stamp = p.origin + p.api.av_rescale_q_rnd(seekUs, Microseconds,
                                                             p.stream->time_base, AV_ROUND_DOWN);
        if (p.api.av_seek_frame(p.format, p.index, stamp, AVSEEK_FLAG_BACKWARD) < 0)
            return p.Failed(reason, "background-video-seek-failed");
        p.api.avcodec_flush_buffers(p.codec);
        p.api.av_packet_unref(p.packet);
        p.current = {};
        p.next = {};
        p.eof = p.draining = false;
    }
    if (!p.current.image && !p.Decode(p.current, reason))
        return false;
    if (!p.current.image)
        return Fail(reason, "background-video-empty");
    p.current.first = p.current.startUs <= 0;
    while (true) {
        if (!p.next.image && !p.eof && !p.Decode(p.next, reason))
            return false;
        if (!p.next.image) {
            p.current.last = true;
            p.current.endUs = std::max(p.current.endUs, p.info.durationUs);
            break;
        }
        if (p.next.startUs <= p.current.startUs)
            return Fail(reason, "background-video-timestamps-not-increasing");
        p.current.endUs = p.next.startUs;
        if (target < p.next.startUs)
            break;
        p.current = std::move(p.next);
        p.next = {};
    }
    if (!p.current.Covers(target)) {
        reason = "background-video-seek-did-not-cover-time:" + std::to_string(target) + ":" +
                 std::to_string(p.current.startUs) + ":" + std::to_string(p.current.endUs);
        return false;
    }
    out = p.current;
    reason.clear();
    return true;
}
bool VideoDecoder::ReadScene(double seconds, bool nativeThirtyHz, media::VideoFrame& out,
                             const media::VideoProceed& proceed, std::string& reason) {
    std::int64_t time;
    if (!impl_ || !media::VideoTime(seconds, time)) {
        out = {};
        return Fail(reason, "background-video-time-invalid-or-not-open");
    }
    const auto& p = *impl_;
    const bool ordinal = p.ordinalThirty && nativeThirtyHz;
    const auto rate = p.stream->avg_frame_rate;
    const auto sample = std::int64_t(seconds * 30 + 1e-7);
    const double mapped = !p.avi    ? seconds
                          : ordinal ? double(sample) * rate.den / rate.num
                                    : double(time / 1000) / 1000;
    if (!Read(std::min(21600., mapped), out, proceed, reason))
        return false;
    if (p.avi) {
        if (ordinal) {
            // The ordinal's complete native 30Hz interval, regardless of the
            // file's nominal 29.97/30 clock. This preserves exact native export.
            out.startUs = sample * 1000000 / 30;
            out.endUs = (sample + 1) * 1000000 / 30;
        } else {
            out.startUs = ((out.startUs + 999) / 1000) * 1000;
            out.endUs = ((out.endUs + 999) / 1000) * 1000;
        }
    }
    return true;
}
}
