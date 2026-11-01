#pragma once
#include <cmath>
#include <cstdint>

namespace mikudancestudio::cpu_edge {
// The x64 worker performs these operations in this order, in single precision.
// Keep contraction disabled in callers. The x86 adapter retains its VC9 path.
inline float Component(float position, float normal, float amount) {
    return position + normal * amount;
}
inline float PmxComponent(float position, float normal, float frameScale, float vertexScale,
                          float materialSize) {
    float amount = frameScale * normal;
    amount *= vertexScale;
    amount *= materialSize;
    return position + amount;
}
inline float DistanceScale(float distance, float fov, float modelScale) {
    return distance * .00004500000068219379f * (fov * .6000000238418579f + 1.f) * modelScale;
}
inline std::uint32_t Color(const float* color, bool opaque) {
    const auto channel = [](float v) -> std::uint32_t {
        const float scaled = v * 255.f;
        // SSE's invalid conversion yields INT_MIN, whose low byte is zero.
        if (!std::isfinite(scaled) || scaled < -2147483648.f || scaled >= 2147483648.f)
            return 0;
        return std::uint8_t(std::int32_t(scaled));
    };
    return ((opaque ? 255 : channel(color[3])) << 24) | (channel(color[0]) << 16) |
           (channel(color[1]) << 8) | channel(color[2]);
}
}
