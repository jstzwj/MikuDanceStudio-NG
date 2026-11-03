#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace mikudancestudio::format {
struct WaveAudio {
    std::uint32_t sampleRate = 0, channels = 0;
    std::uint32_t sourceBlockAlign = 0;
    std::vector<float> samples;
    std::uint64_t SampleFrames() const { return channels ? samples.size() / channels : 0; }
    std::uint64_t FrameOffset(std::uint32_t mmdFrame) const;
};
// RIFF PCM 8/16/24/32 and IEEE float32, mono/stereo. Normalized float output;
// rejects malformed alignment, unsupported encodings and nonfinite samples.
bool ReadWave(const std::vector<std::uint8_t>&, WaveAudio&, std::string&);
}
