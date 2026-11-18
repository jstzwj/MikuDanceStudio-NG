#pragma once
#include "mikudancestudio/background_video.hpp"
#include "media_budget.hpp"
#include <filesystem>

namespace mikudancestudio::qt {
// Synchronous bounded decoder, owned by one worker. No QObject event loop,
// audio output, external ffmpeg executable, GPU handles or visible surfaces.
class VideoDecoder final {
public:
    explicit VideoDecoder(std::chrono::milliseconds timeout = MediaBudget::VideoTimeout);
    ~VideoDecoder();
    bool Open(const std::filesystem::path&, std::string&, const media::VideoProceed& = {});
    const media::VideoInfo& Info() const;
    std::size_t CachedBytes() const;
    bool Read(double seconds, media::VideoFrame&, const media::VideoProceed&, std::string&);
    // AVI compatibility: native paused/30fps export may address a 33ms stream
    // by ordinal. Other AVI picks truncate to milliseconds like VFW. Modern
    // containers always use PTS. Returned coverage is in scene time.
    bool ReadScene(double seconds, bool nativeThirtyHz, media::VideoFrame&,
                   const media::VideoProceed&, std::string&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::chrono::milliseconds timeout_;
};
enum class VideoReady {
    Pending,
    Ready,
    Failed
};
struct VideoPreviewStats {
    std::size_t decoderBytes = 0, publishedBytes = 0;
    unsigned workers = 0, inFlight = 0, queued = 0, openDecoders = 0;
};
// One active decode, one replaceable request, one published frame. A newer
// generation cancels older work; an obsolete result can never be published.
class VideoPreview final {
public:
    VideoPreview();
    ~VideoPreview();
    VideoReady Request(const std::filesystem::path&, double seconds, media::VideoFrame&,
                       std::string&, bool nativeThirtyHz = true, bool supersede = true);
    std::string Failure() const;
    VideoPreviewStats Stats() const;
    void Clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
inline media::VideoFrameSource ExportVideoSource(const std::filesystem::path& path,
                                                 VideoDecoder& decoder,
                                                 bool nativeThirtyHz = true) {
    return [path, &decoder, nativeThirtyHz, opened = false](double seconds, media::VideoFrame& out,
                                                            const media::VideoProceed& proceed,
                                                            std::string& reason) mutable {
        if (!opened) {
            if (!decoder.Open(path, reason, proceed))
                return false;
            opened = true;
        }
        return decoder.ReadScene(seconds, nativeThirtyHz, out, proceed, reason);
    };
}
}
