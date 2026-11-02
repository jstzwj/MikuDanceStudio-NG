#include "audio_output.hpp"
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <algorithm>
namespace mikudancestudio::qt {
namespace {
class NativeAudioOutput final : public AudioOutput {
public:
    bool Start(const QAudioFormat& format, QIODevice& source, float volume,
               std::string& reason) override {
        Stop();
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(format)) {
            reason = "audio-output-format-unavailable";
            return false;
        }
        sink_ = std::make_unique<QAudioSink>(device, format);
        format_ = format;
        source_ = &source;
        // Leave enough queued data for the platform's timer scheduling quantum.
        // The platform may round this up; the clock uses its actual padding.
        sink_->setBufferSize(format.bytesForDuration(100000));
        sink_->setVolume(volume);
        sink_->start(&source);
        if (sink_->error() != QAudio::NoError) {
            reason = "audio-output-start-failed";
            Stop();
            return false;
        }
        reason.clear();
        return true;
    }
    void Stop() override {
        if (sink_)
            // Pause/seek discard queued audio. PulseAudio stop() drains it and
            // can block the UI; reset() flushes it before destroying the sink.
            sink_->reset();
        sink_.reset();
        source_ = nullptr;
        processed_ = 0;
    }
    void Volume(float volume) override {
        if (sink_)
            sink_->setVolume(volume);
    }
    bool Running() const override { return sink_ && sink_->state() == QAudio::ActiveState; }
    std::string Error() const override {
        // Idle at the end of a finite source is normal. Underrun while source
        // data remains and device/IO errors must not silently switch clocks.
        if (!sink_ || sink_->error() == QAudio::NoError ||
            (sink_->error() == QAudio::UnderrunError && source_ && source_->atEnd()))
            return {};
        return "audio-output-device-failed";
    }
    qint64 ProcessedMicroseconds() const override {
        if (!sink_)
            return 0;
        auto processed = sink_->processedUSecs();
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0) &&                                \
    QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
        // Qt 6.8's WASAPI implementation reports totalInputBytes, including
        // samples still queued on the device. Subtract its current padding,
        // expressed in the input format by the public buffer queries. Other
        // platform backends already report played samples; do not correct twice.
        const auto queued = std::max<qsizetype>(0, sink_->bufferSize() - sink_->bytesFree());
        processed = std::max<qint64>(0, processed - format_.durationForBytes(queued));
#endif
        processed_ = std::max(processed_, processed);
        return processed_;
    }

private:
    std::unique_ptr<QAudioSink> sink_;
    QAudioFormat format_;
    QIODevice* source_ = nullptr;
    mutable qint64 processed_ = 0;
};
}
std::unique_ptr<AudioOutput> CreateAudioOutput() {
    return std::make_unique<NativeAudioOutput>();
}
}
