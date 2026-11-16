#include "video_encoder.hpp"
#include "editor_window.hpp"
#include "background_video.hpp"
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QMediaCaptureSession>
#include <QVideoFrameInput>
#include <QAudioBufferInput>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <fstream>
#include <cstring>
namespace mikudancestudio::qt {
struct VideoEncoder::Impl {
    QMediaCaptureSession session;
    QMediaRecorder recorder;
    std::unique_ptr<QVideoFrameInput> video;
    std::unique_ptr<QAudioBufferInput> audio;
    QAudioFormat audioFormat;
    QVideoFrameFormat videoFormat;
    render::ExportTimeline timeline;
    const format::WaveAudio* wave = nullptr;
    std::function<bool()> proceed;
    std::uint64_t audioFrames = 0;
    unsigned videoFrames = 0;
    bool videoReady = false, audioReady = false, finishing = false, finished = false;
    std::string failure;
    QEventLoop* waiting = nullptr;
    void Wake() {
        if (waiting)
            waiting->quit();
    }
    template <class Predicate> bool Wait(Predicate ready, std::string& reason) {
        QElapsedTimer deadline;
        deadline.start();
        QEventLoop loop;
        QTimer cancelPoll;
        QObject::connect(&cancelPoll, &QTimer::timeout, &loop, &QEventLoop::quit);
        cancelPoll.start(10);
        while (!ready()) {
            if (!Check(reason))
                return false;
            if (deadline.elapsed() > 30000) {
                reason = "export-encoder-stalled";
                return false;
            }
            waiting = &loop;
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            waiting = nullptr;
        }
        return Check(reason);
    }
    bool Check(std::string& reason) {
        if (!failure.empty()) {
            reason = failure;
            return false;
        }
        if (proceed && !proceed()) {
            reason = "export-cancelled";
            return false;
        }
        return true;
    }
    bool AudioUntil(std::uint64_t end, std::string& reason) {
        if (!audio)
            return true;
        while (audioFrames < end) {
            if (!Wait([&] { return audioReady; }, reason))
                return false;
            const auto count = unsigned(std::min<std::uint64_t>(1024, end - audioFrames));
            QByteArray bytes(qsizetype(count) * wave->channels * sizeof(float), 0);
            const auto first = timeline.SourceAudioSample(wave->sampleRate) + audioFrames;
            const auto available =
                first < wave->SampleFrames()
                    ? std::min<std::uint64_t>(count, wave->SampleFrames() - first)
                    : 0;
            if (available)
                std::memcpy(bytes.data(), wave->samples.data() + first * wave->channels,
                            std::size_t(available * wave->channels * sizeof(float)));
            const auto stamp = qint64(audioFrames * 1000000 / wave->sampleRate);
            audioReady = false;
            if (!audio->sendAudioBuffer(QAudioBuffer(bytes, audioFormat, stamp))) {
                reason = "export-audio-input-rejected";
                return false;
            }
            audioFrames += count;
        }
        return true;
    }
};
VideoEncoder::VideoEncoder() : impl_(std::make_unique<Impl>()) {
}
VideoEncoder::~VideoEncoder() {
    if (impl_ && !impl_->finished) {
        // stop() finalizes asynchronously. Keep the event dispatcher and all
        // inputs alive until the muxer releases its file (notably on Windows).
        auto& i = *impl_;
        i.proceed = {};
        i.failure.clear();
        i.finishing = true;
        const bool stopped = i.recorder.recorderState() == QMediaRecorder::StoppedState;
        i.recorder.stop();
        std::string ignored;
        if (!stopped)
            i.Wait([&] { return i.finished; }, ignored);
    }
}
bool VideoEncoder::Open(const std::filesystem::path& path, const render::ExportTimeline& timeline,
                        VideoProfile profile, const format::WaveAudio* wave,
                        const std::function<bool()>& proceed, std::string& reason) {
    auto& i = *impl_;
    const auto& s = timeline.settings;
    if (s.alpha != render::ExportAlpha::Opaque || (s.width & 1) || (s.height & 1) ||
        (profile != VideoProfile::Mp4 && profile != VideoProfile::Mp4Pcm)) {
        reason = "video-requires-opaque-alpha-and-even-size";
        return false;
    }
    if (wave &&
        (!wave->sampleRate || wave->sampleRate > 192000 || !wave->channels || wave->channels > 2)) {
        reason = "export-audio-format-unsupported";
        return false;
    }
    i.timeline = timeline;
    i.wave = wave;
    i.proceed = proceed;
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(profile == VideoProfile::Mp4 ? QMediaFormat::VideoCodec::MPEG4
                                                      : QMediaFormat::VideoCodec::MotionJPEG);
    if (wave)
        format.setAudioCodec(profile == VideoProfile::Mp4 ? QMediaFormat::AudioCodec::AAC
                                                          : QMediaFormat::AudioCodec::Wave);
    if (!format.isSupported(QMediaFormat::Encode)) {
        reason = "export-codec-unavailable";
        return false;
    }
    QVideoFrameFormat videoFormat(QSize(int(s.width), int(s.height)),
                                  QVideoFrameFormat::Format_RGBA8888);
    videoFormat.setFrameRate(double(s.rateNumerator) / s.rateDenominator);
    videoFormat.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
    videoFormat.setColorTransfer(QVideoFrameFormat::ColorTransfer_BT709);
    videoFormat.setColorRange(QVideoFrameFormat::ColorRange_Full);
    i.videoFormat = videoFormat;
    i.video = std::make_unique<QVideoFrameInput>(videoFormat);
    QObject::connect(i.video.get(), &QVideoFrameInput::readyToSendVideoFrame, &i.session, [&i] {
        i.videoReady = true;
        i.Wake();
    });
    i.session.setVideoFrameInput(i.video.get());
    if (wave) {
        i.audioFormat.setSampleRate(int(wave->sampleRate));
        i.audioFormat.setChannelCount(int(wave->channels));
        i.audioFormat.setSampleFormat(QAudioFormat::Float);
        i.audio = std::make_unique<QAudioBufferInput>(i.audioFormat);
        QObject::connect(i.audio.get(), &QAudioBufferInput::readyToSendAudioBuffer, &i.session,
                         [&i] {
                             i.audioReady = true;
                             i.Wake();
                         });
        i.session.setAudioBufferInput(i.audio.get());
        i.recorder.setAudioSampleRate(int(wave->sampleRate));
        i.recorder.setAudioChannelCount(int(wave->channels));
        i.recorder.setAudioBitRate(192000);
    }
    QObject::connect(&i.recorder, &QMediaRecorder::errorOccurred, &i.session,
                     [&i](QMediaRecorder::Error, const QString& text) {
                         i.failure = "export-encoder:" + text.toStdString();
                         i.Wake();
                     });
    QObject::connect(&i.recorder, &QMediaRecorder::recorderStateChanged, &i.session,
                     [&i](QMediaRecorder::RecorderState state) {
                         if (state == QMediaRecorder::StoppedState && i.finishing)
                             i.finished = true;
                         else if (state == QMediaRecorder::StoppedState && i.videoFrames)
                             i.failure = "export-encoder-stopped-early";
                         i.Wake();
                     });
    i.recorder.setMediaFormat(format);
    i.recorder.setVideoResolution(int(s.width), int(s.height));
    i.recorder.setVideoFrameRate(double(s.rateNumerator) / s.rateDenominator);
    i.recorder.setQuality(QMediaRecorder::VeryHighQuality);
    if (profile == VideoProfile::Mp4) {
        i.recorder.setEncodingMode(QMediaRecorder::ConstantBitRateEncoding);
        i.recorder.setVideoBitRate(
            int(std::clamp(double(s.width) * s.height * s.rateNumerator / s.rateDenominator * 4,
                           2000000., 100000000.)));
    }
    i.recorder.setAutoStop(true);
    i.recorder.setOutputLocation(QUrl::fromLocalFile(Path(path)));
    i.session.setRecorder(&i.recorder);
    if (!i.recorder.isAvailable()) {
        reason = "export-recorder-unavailable";
        return false;
    }
    i.recorder.record();
    if (!i.Wait([&] { return i.videoReady && (!wave || i.audioReady); }, reason))
        return false;
    const auto actual = i.recorder.mediaFormat();
    if (actual.fileFormat() != format.fileFormat() || actual.videoCodec() != format.videoCodec() ||
        (wave && actual.audioCodec() != format.audioCodec())) {
        reason = "export-codec-substitution-rejected";
        return false;
    }
    return true;
}
bool VideoEncoder::Write(const render::ExportFrame& frame, std::string& reason) {
    auto& i = *impl_;
    const auto& s = i.timeline.settings;
    if (frame.index != i.videoFrames || frame.rgba.size() != std::size_t(s.width) * s.height * 4 ||
        frame.index >= i.timeline.frames) {
        reason = "export-video-frame-invalid";
        return false;
    }
    if (!i.Wait([&] { return i.videoReady; }, reason))
        return false;
    QVideoFrame video(i.videoFormat);
    if (!video.map(QVideoFrame::WriteOnly)) {
        reason = "export-video-buffer-map-failed";
        return false;
    }
    for (unsigned y = 0; y < s.height; ++y)
        std::memcpy(video.bits(0) + y * video.bytesPerLine(0),
                    frame.rgba.data() + std::size_t(y) * s.width * 4, s.width * 4);
    video.unmap();
    video.setStartTime(frame.startUs);
    video.setEndTime(frame.endUs);
    i.videoReady = false;
    if (!i.video->sendVideoFrame(video)) {
        reason = "export-video-input-rejected";
        return false;
    }
    // Start video before sending multiple audio blocks. Recorder initialization
    // waits for both first inputs; filling the bounded audio input first can
    // otherwise deadlock when one video period spans more than 1024 samples.
    if (!i.AudioUntil(i.wave ? i.timeline.AudioSample(frame.index + 1, i.wave->sampleRate) : 0,
                      reason))
        return false;
    ++i.videoFrames;
    return true;
}
bool VideoEncoder::Finish(std::string& reason) {
    auto& i = *impl_;
    if (i.videoFrames != i.timeline.frames) {
        reason = "export-video-incomplete";
        return false;
    }
    if (!i.Wait([&] { return i.videoReady && (!i.audio || i.audioReady); }, reason))
        return false;
    i.finishing = true;
    if (!i.video->sendVideoFrame({}) || (i.audio && !i.audio->sendAudioBuffer({}))) {
        reason = "export-end-of-stream-rejected";
        return false;
    }
    if (!i.Wait([&] { return i.finished; }, reason))
        return false;
    reason.clear();
    return true;
}
bool ExportMovie(const render::ExportSnapshot& snapshot, const std::filesystem::path& builtins,
                 const rhi::BackendOptions& options, const std::filesystem::path& destination,
                 const render::ExportSettings& settings, VideoProfile profile, bool audio,
                 const ExportProgress& progress, std::string& reason) {
    render::ExportTimeline timeline;
    if (!render::MakeExportTimeline(settings, timeline, reason))
        return false;
    const auto target = std::filesystem::absolute(destination).lexically_normal();
    if (Path(target.extension()).toLower() != ".mp4") {
        reason = "export-video-extension-mismatch";
        return false;
    }
    if (std::filesystem::exists(target)) {
        reason = "export-destination-exists";
        return false;
    }
    if (!std::filesystem::is_directory(target.parent_path())) {
        reason = "export-parent-directory-missing";
        return false;
    }
    QTemporaryDir staging(Path(target.parent_path() / ".mds-video-XXXXXX"));
    if (!staging.isValid()) {
        reason = "export-staging-unavailable";
        return false;
    }
    format::WaveAudio wave;
    if (audio && !snapshot.assets.wave.empty()) {
        std::ifstream file(snapshot.assets.wave, std::ios::binary | std::ios::ate);
        const auto size = file ? file.tellg() : std::streampos(-1);
        if (size < 0 || size > 64 * 1024 * 1024) {
            reason = "export-wave-read-or-size-failed";
            return false;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
            !format::ReadWave(bytes, wave, reason))
            return false;
    }
    const auto temporary = Path(staging.path()) / "video.mp4";
    unsigned completed = 0;
    {
        VideoEncoder encoder;
        auto proceed = [&] { return !progress || progress(completed, timeline.frames); };
        if (!encoder.Open(temporary, timeline, profile, wave.sampleRate ? &wave : nullptr, proceed,
                          reason))
            return false;
        VideoDecoder background;
        if (!render::RunExport(
                snapshot, builtins, options, settings,
                [&](const render::ExportFrame& frame, std::string& r) {
                    if (!encoder.Write(frame, r))
                        return false;
                    completed = frame.index + 1;
                    return true;
                },
                progress, reason,
                ExportVideoSource(snapshot.assets.videoPath, background,
                                  timeline.settings.rateNumerator == 30 &&
                                      timeline.settings.rateDenominator == 1)) ||
            !encoder.Finish(reason))
            return false;
    }
    if (progress && !progress(timeline.frames, timeline.frames)) {
        reason = "export-cancelled";
        return false;
    }
    // QFile rename does not overwrite an existing path, including a race after
    // initial validation. The temporary directory is always private to this job.
    if (!QFile::rename(Path(temporary), Path(target))) {
        reason = "export-publish-failed";
        return false;
    }
    reason.clear();
    return true;
}
}
