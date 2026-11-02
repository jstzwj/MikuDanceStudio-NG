#include "audio_output.hpp"
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QBuffer>
#include <QThread>
#include <algorithm>
#include <sstream>
namespace mikudancestudio::qt {
#if defined(Q_OS_LINUX)
std::unique_ptr<AudioOutput> CreatePulseAudioOutput();
#endif
namespace {
class NativeAudioEngine final {
public:
    bool Start(const QAudioDevice& device, const QAudioFormat& format, const QByteArray& pcm,
               qint64 offset, float volume, std::string& reason) {
        Stop();
        if (offset < 0 || offset > pcm.size()) {
            reason = "audio-output-source-unavailable";
            return false;
        }
        pcm_ = pcm;
        source_ = std::make_unique<QBuffer>();
        // Qt's native adapters use QIODevice::size() to configure short-stream
        // prebuffering. A seek into the full WAV misreports the available tail
        // and can leave it below PulseAudio's prebuffer forever. Expose only
        // the remaining immutable PCM; retain its shared owner without copying.
        source_->setData(QByteArray::fromRawData(pcm_.constData() + offset, pcm_.size() - offset));
        if (!source_->open(QIODevice::ReadOnly)) {
            reason = "audio-output-source-unavailable";
            Stop();
            return false;
        }
        sink_ = std::make_unique<QAudioSink>(device, format);
        format_ = format;
        duration_ = format.durationForBytes(pcm.size() - offset);
        // Leave enough queued data for the platform's timer scheduling quantum.
        // The platform may round this up; the clock uses its actual padding.
        sink_->setBufferSize(format.bytesForDuration(100000));
        sink_->setVolume(volume);
        sink_->start(source_.get());
        if (sink_->error() != QAudio::NoError) {
            reason = "audio-output-start-failed";
            Stop();
            return false;
        }
        reason.clear();
        return true;
    }
    void Stop() {
        if (sink_)
            // Pause/seek discard queued audio. PulseAudio stop() drains it and
            // can block the UI; reset() flushes it before destroying the sink.
            sink_->reset();
        sink_.reset();
        source_.reset();
        pcm_.clear();
        processed_ = 0;
        duration_ = 0;
    }
    void Volume(float volume) {
        if (sink_)
            sink_->setVolume(volume);
    }
    bool Running() const {
        if (!sink_)
            return false;
        if (sink_->state() == QAudio::ActiveState)
            return true;
        // PulseAudio can enter Idle when the server has consumed the source
        // while its processed clock still accounts for device latency. Keep
        // that clock through the tail; EOF is not the source's read cursor.
        return sink_->state() == QAudio::IdleState && source_ && source_->atEnd() &&
               Error().empty() && ProcessedMicroseconds() < duration_;
    }
    std::string Error() const {
        // Idle at the end of a finite source is normal. Underrun while source
        // data remains and device/IO errors must not silently switch clocks.
        if (!sink_ || sink_->error() == QAudio::NoError ||
            (sink_->error() == QAudio::UnderrunError && source_ && source_->atEnd()))
            return {};
        return "audio-output-device-failed";
    }
    qint64 ProcessedMicroseconds() const {
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
    std::string Diagnostic() const {
        if (!sink_)
            return "no-sink";
        std::ostringstream result;
        result << "state=" << int(sink_->state()) << " error=" << int(sink_->error())
               << " source=" << (source_ ? source_->pos() : -1) << '/'
               << (source_ ? source_->size() : -1) << " duration-us=" << duration_
               << " processed-us=" << processed_ << " raw-processed-us=" << sink_->processedUSecs()
               << " elapsed-us=" << sink_->elapsedUSecs() << " buffer=" << sink_->bufferSize()
               << " free=" << sink_->bytesFree();
        return result.str();
    }

private:
    std::unique_ptr<QAudioSink> sink_;
    QAudioFormat format_;
    std::unique_ptr<QBuffer> source_;
    QByteArray pcm_;
    mutable qint64 processed_ = 0;
    qint64 duration_ = 0;
};
class NativeAudioOutput final : public AudioOutput {
public:
    NativeAudioOutput() : context_(new QObject) {
        context_->moveToThread(&thread_);
        QObject::connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
        thread_.setObjectName("MMD audio output");
        thread_.start();
    }
    ~NativeAudioOutput() override {
        Stop();
        thread_.quit();
        thread_.wait();
    }
    bool Start(const QAudioFormat& format, QIODevice& source, float volume,
               std::string& reason) override {
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(format)) {
            reason = "audio-output-format-unavailable";
            return false;
        }
        // Transport supplies immutable decoded PCM in a QBuffer. Give the
        // audio thread its own shared-data cursor; never read a UI-owned device
        // across threads or depend on the editor/render event loop to refill it.
        const auto* buffer = qobject_cast<QBuffer*>(&source);
        if (!buffer) {
            reason = "audio-output-source-unavailable";
            return false;
        }
        const auto pcm = buffer->data();
        const auto offset = source.pos();
        bool result = false;
        Invoke([&] { result = engine_.Start(device, format, pcm, offset, volume, reason); });
        return result;
    }
    void Stop() override {
        Invoke([&] { engine_.Stop(); });
    }
    void Volume(float volume) override {
        Invoke([&] { engine_.Volume(volume); });
    }
    bool Running() const override {
        bool result = false;
        Invoke([&] { result = engine_.Running(); });
        return result;
    }
    std::string Error() const override {
        std::string result;
        Invoke([&] { result = engine_.Error(); });
        return result;
    }
    qint64 ProcessedMicroseconds() const override {
        qint64 result = 0;
        Invoke([&] { result = engine_.ProcessedMicroseconds(); });
        return result;
    }
    std::string Diagnostic() const override {
        std::string result;
        Invoke([&] { result = engine_.Diagnostic(); });
        return result;
    }

private:
    template <class F> void Invoke(F&& function) const {
        QMetaObject::invokeMethod(context_, std::forward<F>(function),
                                  Qt::BlockingQueuedConnection);
    }
    QThread thread_;
    QObject* context_;
    NativeAudioEngine engine_;
};
}
std::unique_ptr<AudioOutput> CreateAudioOutput() {
#if defined(Q_OS_LINUX)
    return CreatePulseAudioOutput();
#else
    return std::make_unique<NativeAudioOutput>();
#endif
}
}
