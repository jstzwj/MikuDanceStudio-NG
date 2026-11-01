#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace mikudancestudio::keyframe_edit {
// PMM preserves a DWORD on disk; the original camera code interprets it as
// signed (cvtdq2ps). Avoid implementation-defined unsigned-to-signed casts.
inline std::int32_t SignedCameraFov(std::uint32_t bits) {
    return static_cast<std::int32_t>(static_cast<std::int64_t>(bits) -
                                     (bits >= 0x80000000u ? 0x100000000ll : 0));
}
// Original cvttss2si returns INT_MIN for NaN and out-of-range results.
inline std::int32_t CameraFovInteger(float value) {
    return !std::isfinite(value) || value >= 2147483648.f || value < -2147483648.f
               ? std::numeric_limits<std::int32_t>::min()
               : static_cast<std::int32_t>(value);
}
}
