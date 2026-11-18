#pragma once
#include "rhi_image.hpp"
#include <functional>
#include <string>

namespace mikudancestudio::media {
// Scene seconds, never decoder/player wall time. Native backgrounds clamp at
// both ends; there is no implicit loop, time offset or background audio.
inline bool VideoTime(double seconds, std::int64_t& microseconds) {
    if (!(seconds >= 0 && seconds <= 21600))
        return false;
    microseconds = std::int64_t(seconds * 1000000. + .5);
    return true;
}
struct VideoInfo {
    unsigned width = 0, height = 0; // display dimensions, after SAR and rotation
    std::int64_t durationUs = 0;
    int clockwiseRotation = 0;
    std::string color;
};
struct VideoFrame {
    std::shared_ptr<const rhi::RgbaImage> image;
    std::int64_t startUs = 0, endUs = 0;
    bool first = false, last = false;
    bool Covers(std::int64_t time) const {
        return image && (first || time >= startUs) && (last || time < endUs);
    }
};
// Transient session state, copied by value into export snapshots. PMM stores
// only the path and visibility; cold reopen initializes seconds at its cursor.
// The immutable CPU lease survives hidden paints and GPU/device replacement.
struct VideoState {
    std::uint64_t identity = 0;
    double seconds = 0;
    bool nativeThirtyHz = true;
    VideoFrame frozen;
};
// Host decoder boundary: tightly packed top-left SDR RGBA8, immutable lease,
// media-relative PTS. Reading is cancellable; success covers the requested time.
using VideoProceed = std::function<bool()>;
using VideoFrameSource =
    std::function<bool(double seconds, VideoFrame&, const VideoProceed&, std::string&)>;
}
