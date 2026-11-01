#pragma once
#include "mikudancestudio/editor_session.hpp"
#include <array>
// Frozen RGB FNV-1a hashes from the independent memory-DIB GDI oracle.
// Each pair is curve-only, then curve with native two-pixel control markers.
inline const std::array<mikudancestudio::editor::InterpolationCurve, 5> kCurveInputs{
    {{20, 20, 107, 107}, {0, 127, 127, 0}, {43, 79, 80, 18}, {0, 0, 0, 0}, {127, 127, 127, 127}}};
inline constexpr std::array<std::uint64_t, 10> kCurveRasterHashes{
    0x64f526cb88ba80a3ull, 0x81ffbf30bd5859f3ull, 0x81a3407f503abc43ull, 0x61a5e82769d677d3ull,
    0xb6c3997dd44fdb65ull, 0x1263b30b919e7047ull, 0xba2cb8fcb040432dull, 0xd7b99e40cee70608ull,
    0x64f526cb88ba80a3ull, 0x354162f09da92034ull};
