#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace mikudancestudio::format {
// MMD writes four overlapping 16-byte windows of the PMM curve and the
// beginning of position.x. Spell out the little-endian bytes rather than
// reading past a curve array or depending on a native record's layout.
inline std::array<std::uint8_t, 64>
EncodeVmdBoneInterpolation(const std::uint8_t* curve, float positionX, bool physicsDisabled) {
    std::array<std::uint8_t, 19> source{};
    std::memcpy(source.data(), curve, 16);
    std::uint32_t bits;
    static_assert(sizeof bits == sizeof positionX);
    std::memcpy(&bits, &positionX, sizeof bits);
    for (unsigned i = 0; i < 3; ++i)
        source[16 + i] = std::uint8_t(bits >> (i * 8));
    std::array<std::uint8_t, 64> result{};
    for (unsigned channel = 0; channel < 4; ++channel)
        std::memcpy(result.data() + channel * 16, source.data() + channel, 16);
    result[2] = physicsDisabled ? 0x63 : 0;
    result[3] = physicsDisabled ? 0x0f : 0;
    return result;
}
// Preserve the native VMD loader's four 16-byte channel blocks; the four
// control points become PMM's channel-major rows, not VMD's first 16 bytes.
inline std::array<std::uint8_t, 16>
DecodeVmdBoneInterpolation(const std::array<std::uint8_t, 64>& source) {
    std::array<std::uint8_t, 16> result;
    for (int channel = 0; channel < 4; ++channel)
        for (int point = 0; point < 4; ++point)
            result[point * 4 + channel] = source[channel * 16 + point * 4];
    return result;
}
}
