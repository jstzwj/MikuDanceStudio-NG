#include "audio_output.hpp"
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
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
            sink_->stop();
        sink_.reset();
    }
    void Volume(float volume) override {
        if (sink_)
            sink_->setVolume(volume);
    }
    bool Running() const override { return sink_ && sink_->state() == QAudio::ActiveState; }
    qint64 ProcessedMicroseconds() const override { return sink_ ? sink_->processedUSecs() : 0; }

private:
    std::unique_ptr<QAudioSink> sink_;
};
}
std::unique_ptr<AudioOutput> CreateAudioOutput() {
    return std::make_unique<NativeAudioOutput>();
}
}
