#include "background_video.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mikudancestudio::qt {
struct VideoPreview::Impl {
    struct Request {
        std::filesystem::path path;
        double seconds = 0;
        std::uint64_t generation = 0;
        bool nativeThirtyHz = true;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<std::uint64_t> generation{0};
    bool stop = false, pending = false;
    Request requested, published;
    media::VideoFrame frame;
    std::string error;
    std::thread worker{[this] { Run(); }};
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
            ++generation;
        }
        wake.notify_one();
        worker.join();
    }
    void Run() {
        std::unique_ptr<VideoDecoder> decoder;
        std::filesystem::path loaded;
        while (true) {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || pending; });
                if (stop)
                    return;
                request = requested;
                pending = false;
            }
            if (request.path.empty()) {
                decoder.reset();
                loaded.clear();
                continue;
            }
            std::string reason;
            media::VideoFrame decoded;
            try {
                if (!decoder || request.path != loaded) {
                    decoder = std::make_unique<VideoDecoder>();
                    if (decoder->Open(request.path, reason,
                                      [&] { return generation == request.generation; }))
                        loaded = request.path;
                    else {
                        decoder.reset();
                        loaded.clear();
                    }
                }
                if (decoder)
                    decoder->ReadScene(
                        request.seconds, request.nativeThirtyHz, decoded,
                        [&] { return generation == request.generation; }, reason);
            } catch (const std::exception& e) {
                decoder.reset();
                loaded.clear();
                decoded = {};
                reason = std::string("background-video-worker-failed:") + e.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (request.generation == generation) {
                    published = request;
                    frame = std::move(decoded);
                    error = std::move(reason);
                }
            }
        }
    }
};
VideoPreview::VideoPreview() : impl_(std::make_unique<Impl>()) {
}
VideoPreview::~VideoPreview() = default;
void VideoPreview::Clear() {
    auto& p = *impl_;
    std::lock_guard<std::mutex> lock(p.mutex);
    if (p.requested.path.empty())
        return;
    p.requested = {{}, 0, ++p.generation};
    p.pending = true;
    p.published = {};
    p.frame = {};
    p.error.clear();
    p.wake.notify_one();
}
VideoReady VideoPreview::Request(const std::filesystem::path& path, double seconds,
                                 media::VideoFrame& frame, std::string& reason,
                                 bool nativeThirtyHz) {
    auto& p = *impl_;
    std::lock_guard<std::mutex> lock(p.mutex);
    frame = {};
    std::int64_t time;
    if (path.empty() || !media::VideoTime(seconds, time)) {
        reason = "background-video-time-or-path-invalid";
        return VideoReady::Failed;
    }
    if (p.published.path == path && p.published.nativeThirtyHz == nativeThirtyHz &&
        p.frame.Covers(time)) {
        frame = p.frame;
        reason.clear();
        return VideoReady::Ready;
    }
    if (p.published.path == path && p.published.generation == p.requested.generation &&
        !p.error.empty()) {
        reason = p.error;
        return VideoReady::Failed;
    }
    if (p.requested.path != path || p.requested.seconds != seconds ||
        p.requested.nativeThirtyHz != nativeThirtyHz) {
        p.requested = {path, seconds, ++p.generation, nativeThirtyHz};
        p.pending = true;
        p.wake.notify_one();
    }
    frame = {};
    reason.clear();
    return VideoReady::Pending;
}
}
