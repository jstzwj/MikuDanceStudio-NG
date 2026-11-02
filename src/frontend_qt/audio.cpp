#include "audio.hpp"
#include "editor_window.hpp"
#include <QFile>
#include <cmath>
namespace mikudancestudio::qt {
AudioTransport::AudioTransport(std::unique_ptr<AudioOutput> output, bool disableNativeOutput)
    : output_(std::move(output)) {
    if (!output_ && !disableNativeOutput)
        output_ = CreateAudioOutput();
}
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
    const auto duration = double(wave_.SampleFrames()) / wave_.sampleRate;
    const auto offset = std::min(wave_.SampleFrames(),
                                 std::uint64_t(std::min(seconds, duration) * wave_.sampleRate));
    start_ = double(offset) / wave_.sampleRate;
    reason.clear();
    if (!output_ || offset == wave_.SampleFrames())
        return true;
    buffer_.open(QIODevice::ReadOnly);
    buffer_.seek(qint64(offset * wave_.channels * 4));
    if (!output_->Start(format, buffer_, volume_, reason)) {
        Stop();
        return false;
    }
    clockActive_ = true;
    return true;
}
void AudioTransport::Stop() {
    clockActive_ = false;
    if (output_)
        output_->Stop();
    buffer_.close();
}
void AudioTransport::Volume(float value) {
    volume_ = std::clamp(value, 0.f, 1.f);
    if (output_)
        output_->Volume(volume_);
}
bool AudioTransport::Running() const {
    return output_ && output_->Running();
}
std::string AudioTransport::Error() const {
    return output_ ? output_->Error() : std::string{};
}
std::string AudioTransport::Diagnostic() const {
    return output_ ? output_->Diagnostic() : std::string{};
}
double AudioTransport::Position() const {
    const auto duration = wave_.sampleRate ? double(wave_.SampleFrames()) / wave_.sampleRate : 0;
    return std::min(duration, start_ + (output_ ? output_->ProcessedMicroseconds() / 1000000. : 0));
}
std::optional<double> AudioTransport::ClockPosition() {
    if (!clockActive_)
        return {};
    // Query completion first: the adapter may observe the final processed
    // sample while deciding that its device-latency tail has finished.
    const bool running = Running();
    const auto position = Position();
    if (!running)
        clockActive_ = false;
    return position;
}
}
