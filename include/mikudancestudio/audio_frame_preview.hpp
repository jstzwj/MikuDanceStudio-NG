#pragma once
#include <cstdint>
namespace mikudancestudio {
// MMD v9.32 x64: unsigned (frame - 1), CVTSI2SS, DIVSS 30,
// then conversion to double for WaveRestartAt.
inline double AudioFramePreviewSeconds(std::uint32_t frame) noexcept {
    return double(float(frame - 1u) / 30.f);
}
inline constexpr int AudioFramePreviewMilliseconds = 33;
inline std::uint64_t AudioFramePreviewSampleOffset(std::uint32_t frame, std::uint32_t sampleRate,
                                                   std::uint32_t blockAlign) noexcept {
    if (!sampleRate || !blockAlign)
        return 0;
    // WaveSeekAndFeed truncates a signed 32-bit byte offset, then aligns it.
    // Overflow produces INT_MIN. SetFilePointer(FILE_BEGIN) rejects that
    // negative seek, retaining the newly opened stream's data start (frame 0).
    const double frames =
        (double(sampleRate * blockAlign) * AudioFramePreviewSeconds(frame)) / blockAlign;
    const double bytes = frames * blockAlign;
    if (bytes >= 2147483648.)
        return 0;
    const auto whole = std::int32_t(bytes);
    return std::uint64_t(whole - whole % std::int32_t(blockAlign)) / blockAlign;
}
}
