#include "audio_samples.hpp"
#include "media_budget.hpp"
#include <QBuffer>
#include <algorithm>
#include <array>
#include <cmath>
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
WaveDevice::WaveDevice(AudioSamples samples, qint64 origin, bool int16Output)
    : samples_(std::move(samples)), origin_(origin), int16Output_(int16Output) {
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}
qint64 WaveDevice::size() const {
    return (samples_.Size() - origin_) / (int16Output_ ? 2 : 1);
}
bool WaveDevice::seek(qint64 offset) {
    if (offset < 0 || offset > size() || !QIODevice::seek(offset))
        return false;
    failure_.clear();
    return true;
}
qint64 WaveDevice::readData(char* output, qint64 requested) {
    const auto bytes = std::min({requested, size() - pos(), qint64(MediaBudget::AudioReadBytes)});
    if (!bytes)
        return 0;
    std::string reason;
    if (bytes > 0) {
        if (int16Output_) {
            const auto read = ReadInt16(output, bytes, reason);
            if (read >= 0)
                return read;
        } else if (samples_.Read(origin_ + pos(), output, bytes, reason)) {
            return bytes;
        }
    }
    failure_ = reason.empty() ? "wave-file-read-failed" : reason;
    setErrorString(QString::fromStdString(failure_));
    return -1;
}
qint64 WaveDevice::ReadInt16(char* output, qint64 bytes, std::string& reason) {
    std::array<float, 4096> input;
    std::array<std::int16_t, 4096> converted;
    const auto skip = pos() % 2;
    const auto take = std::min(bytes, qint64(sizeof converted) - skip);
    const auto samples = (take + skip + 1) / 2;
    if (!samples_.Read(origin_ + (pos() / 2) * 4, reinterpret_cast<char*>(input.data()),
                       samples * 4, reason))
        return -1;
    for (qint64 i = 0; i < samples; ++i)
        converted[i] = std::int16_t(
            std::clamp(std::lround(std::clamp(input[i], -1.f, 1.f) * 32768.f), -32768L, 32767L));
    std::memcpy(output, reinterpret_cast<const char*>(converted.data()) + skip, std::size_t(take));
    return take;
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
