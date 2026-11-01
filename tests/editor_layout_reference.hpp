#pragma once
#include <cstdint>
// Captured from hash-verified MMD v9.32 x64's RVA 1D250, by
// editor_layout_oracle_test. Each hash covers the ordered 146 MoveWindow
// records in all 128 camera/panel option combinations. Integers are LE32;
// FNV-1a starts at 14695981039346656037. No original binary is redistributed.
struct OriginalLayoutReference {
    int sidebar, bottom;
    std::uint64_t hash;
};
inline constexpr OriginalLayoutReference kOriginalLayouts[]{
    {250, 480, 0x72094c610f406de5ull},  {250, 700, 0x8ca62807ad5ef0fdull},
    {381, 900, 0x2c4b684e6890f90dull},  {511, 1024, 0xfe2c2a5eb9be39a5ull},
    {640, 1080, 0xfdb1c532f8e12635ull}, {960, 1800, 0x3c340641aa77ec55ull}};
// Independent viewport-strip tests exercise each inclusive fit boundary.
inline constexpr int kViewportFitBoundaries[]{5,   140, 157, 213, 230, 286, 303, 400, 417, 458, 475,
                                              516, 533, 579, 616, 195, 250, 305, 360, 398, 441};
// RVA 1FA90: 378 states each, main-window inset then separate-window inset.
inline constexpr std::uint64_t kOriginalViewportLayouts[]{0x4becc342af12dfe6ull,
                                                          0x25cae338d64e521aull};
