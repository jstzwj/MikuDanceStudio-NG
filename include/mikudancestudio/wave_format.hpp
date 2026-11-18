#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <filesystem>
#include <memory>
namespace mikudancestudio::format {
// Immutable logical PCM. Reads are bounded and independently positioned;
// native audio workers may share the source. Past EOF produces silence.
struct WaveSource {
    std::uint32_t sampleRate = 0, channels = 0;
    std::uint32_t sourceBlockAlign = 0;
    static constexpr unsigned MaxReadFrames = 8192;
    virtual ~WaveSource() = default;
    virtual std::uint64_t SampleFrames() const = 0;
    virtual bool ReadFrames(std::uint64_t first, unsigned count, float*, std::string&) const = 0;
    std::uint64_t FrameOffset(std::uint32_t mmdFrame) const;
};
struct WaveAudio : WaveSource {
    std::vector<float> samples;
    std::uint64_t SampleFrames() const override { return channels ? samples.size() / channels : 0; }
    bool ReadFrames(std::uint64_t first, unsigned count, float*, std::string&) const override;
};
// RIFF PCM 8/16/24/32 and IEEE float32, mono/stereo. Normalized float output;
// rejects malformed alignment, unsupported encodings and nonfinite samples.
bool ReadWave(const std::vector<std::uint8_t>&, WaveAudio&, std::string&);
// Local RIFF, same PCM/float contract, up to six hours. Skips payloads during
// bounded header scanning; validates samples lazily. Failed open preserves out.
bool OpenWaveFile(const std::filesystem::path&, std::shared_ptr<const WaveSource>&, std::string&);
}
