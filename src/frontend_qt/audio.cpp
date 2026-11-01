#include "audio.hpp"
#include "editor_window.hpp"
#include <QFile>
#include <QMediaDevices>
#include <QAudioDevice>
#include <cmath>
namespace mikudancestudio::qt {
bool AudioTransport::Load(const std::filesystem::path& path, std::string& reason) {
    QFile file(qt::Path(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > (256u << 20)) {
        reason = "wave-read-failed";
        return false;
    }
    const auto bytes = file.readAll();
    std::vector<std::uint8_t> input(bytes.begin(), bytes.end());
    format::WaveAudio next;
    if (!format::ReadWave(input, next, reason))
        return false;
    Stop();
    wave_ = std::move(next);
    path_ = path;
    pcm_ = QByteArray(reinterpret_cast<const char*>(wave_.samples.data()),
                      qsizetype(wave_.samples.size() * 4));
    buffer_.setData(pcm_);
    return true;
}
bool AudioTransport::Play(double seconds, std::string& reason) {
    if (!std::isfinite(seconds) || seconds < 0 || !wave_.sampleRate) {
        reason = "invalid-audio-start";
        return false;
    }
    Stop();
    QAudioFormat format;
    format.setSampleRate(int(wave_.sampleRate));
    format.setChannelCount(int(wave_.channels));
    format.setSampleFormat(QAudioFormat::Float);
    const auto device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(format)) {
        reason = "audio-output-format-unavailable";
        return false;
    }
    const auto offset = std::min(wave_.SampleFrames(), std::uint64_t(seconds * wave_.sampleRate));
    start_ = double(offset) / wave_.sampleRate;
    buffer_.open(QIODevice::ReadOnly);
    buffer_.seek(qint64(offset * wave_.channels * 4));
    sink_ = std::make_unique<QAudioSink>(device, format);
    sink_->setVolume(volume_);
    sink_->start(&buffer_);
    if (sink_->error() != QAudio::NoError) {
        reason = "audio-output-start-failed";
        Stop();
        return false;
    }
    return true;
}
void AudioTransport::Stop() {
    if (sink_)
        sink_->stop();
    sink_.reset();
    buffer_.close();
}
void AudioTransport::Volume(float value) {
    volume_ = std::clamp(value, 0.f, 1.f);
    if (sink_)
        sink_->setVolume(volume_);
}
bool AudioTransport::Running() const {
    return sink_ && sink_->state() == QAudio::ActiveState;
}
double AudioTransport::Position() const {
    return start_ + (sink_ ? sink_->processedUSecs() / 1000000. : 0);
}
}
