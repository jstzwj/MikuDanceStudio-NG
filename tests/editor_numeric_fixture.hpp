#pragma once
#include <array>
#include <cstdint>
#include <cstring>
inline std::array<float, 7> NumericCameraInput(unsigned i) {
    return {float(int(i) - 128) / 8,
            float(i) / 16,
            -float(i) / 32,
            float(int(i) - 100) / 16,
            float(int(i) - 73) / 32,
            float(int(i) - 198) / 64,
            -float(i) / 4};
}
inline std::array<float, 7> NumericCameraEdited(unsigned i) {
    return {-float(i) / 16,          float(int(i) - 8) / 8,  float(i) / 4, float(int(i) - 128) * 2,
            float(int(i) - 113) * 4, float(int(i) - 99) / 2, float(i) / 8};
}
inline void NumericHash(std::uint64_t& hash, const std::array<float, 7>& values) {
    for (float value : values) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((bits >> shift) & 255)) * 1099511628211ull;
    }
}
