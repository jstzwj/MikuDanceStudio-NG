#include "audio_samples.hpp"
#include <QBuffer>
#include <algorithm>
#include <array>
#include <cstring>
namespace mikudancestudio::qt {
qint64 AudioDurationMicroseconds(const QAudioFormat& format, qint64 bytes) {
    if (bytes <= 0 || format.bytesPerFrame() <= 0 || format.sampleRate() <= 0)
        return 0;
    const auto frames = bytes / format.bytesPerFrame();
    return (frames / format.sampleRate()) * 1000000 +
           (frames % format.sampleRate()) * 1000000 / format.sampleRate();
}
qint64 AudioSamples::Size() const {
    return wave ? qint64(wave->SampleFrames() * wave->channels * sizeof(float)) : pcm.size();
}
bool AudioSamples::Read(qint64 offset, char* out, qint64 bytes, std::string& reason) const {
    if (offset < 0 || bytes < 0 || offset > Size() || bytes > Size() - offset) {
        reason = "audio-output-source-unavailable";
        return false;
    }
    if (!wave) {
        if (bytes)
            std::memcpy(out, pcm.constData() + offset, std::size_t(bytes));
        reason.clear();
        return true;
    }
    if (wave->channels < 1 || wave->channels > 2) {
        reason = "audio-output-format-or-source-unavailable";
        return false;
    }
    std::array<float, format::WaveSource::MaxReadFrames * 2> block;
    const auto align = qint64(wave->channels * sizeof(float));
    while (bytes) {
        const auto skip = offset % align;
        const auto take = std::min(bytes, qint64(format::WaveSource::MaxReadFrames) * align - skip);
        const auto frames = unsigned((take + skip + align - 1) / align);
        if (!wave->ReadFrames(std::uint64_t(offset / align), frames, block.data(), reason))
            return false;
        std::memcpy(out, reinterpret_cast<const char*>(block.data()) + skip, std::size_t(take));
        offset += take;
        out += take;
        bytes -= take;
    }
    reason.clear();
    return true;
}
WaveDevice::WaveDevice(AudioSamples samples, qint64 origin)
    : samples_(std::move(samples)), origin_(origin) {
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}
qint64 WaveDevice::size() const {
    return samples_.Size() - origin_;
}
bool WaveDevice::seek(qint64 offset) {
    if (offset < 0 || offset > size() || !QIODevice::seek(offset))
        return false;
    failure_.clear();
    return true;
}
qint64 WaveDevice::readData(char* output, qint64 requested) {
    const auto bytes = std::min({requested, size() - pos(), qint64(65536)});
    std::string reason;
    if (bytes < 0 || !samples_.Read(origin_ + pos(), output, bytes, reason)) {
        failure_ = reason.empty() ? "wave-file-read-failed" : reason;
        setErrorString(QString::fromStdString(reason));
        return -1;
    }
    return bytes;
}
bool CaptureAudioSamples(QIODevice& source, AudioSamples& output, std::string& reason) {
    if (const auto* wave = dynamic_cast<const WaveDevice*>(&source))
        output = wave->Samples();
    else if (const auto* buffer = qobject_cast<const QBuffer*>(&source))
        output = {{}, buffer->data()};
    else {
        reason = "audio-output-source-unavailable";
        return false;
    }
    reason.clear();
    return true;
}
}
