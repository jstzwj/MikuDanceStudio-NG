#include "mikudancestudio/editor_session.hpp"

namespace mikudancestudio::editor {
void EditorSession::RefreshVideo() {
    state_.assets.video = {++nextVideoIdentity_, CursorSeconds(), !Playing(), {}};
}
VideoRequest EditorSession::RequestVideo() const {
    const auto& assets = Assets();
    return {assets.videoPath, assets.video.identity,
            Document().avi.enabled ? CursorSeconds() : assets.video.seconds,
            Document().avi.enabled ? !Playing() : assets.video.nativeThirtyHz};
}
bool EditorSession::PublishVideo(const VideoRequest& request, const media::VideoFrame& frame,
                                 std::string& reason) {
    const auto current = RequestVideo();
    std::int64_t time;
    if (request.path.empty() || request.path != current.path ||
        request.identity != current.identity || request.seconds != current.seconds ||
        request.nativeThirtyHz != current.nativeThirtyHz) {
        reason = "background-video-publication-expired";
        return false;
    }
    if (!media::VideoTime(request.seconds, time) || !frame.Covers(time) || !frame.image->width ||
        !frame.image->height || frame.image->width > 8192 || frame.image->height > 8192 ||
        std::uint64_t(frame.image->width) * frame.image->height > 16777216 ||
        frame.image->pixels.size() != std::size_t(frame.image->width) * frame.image->height * 4) {
        reason = "background-video-publication-invalid";
        return false;
    }
    auto& state = state_.assets.video;
    state.seconds = request.seconds;
    state.nativeThirtyHz = request.nativeThirtyHz;
    state.frozen = frame;
    reason.clear();
    return true;
}
}
