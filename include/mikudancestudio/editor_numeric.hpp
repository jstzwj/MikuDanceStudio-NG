#pragma once
#include "cpu_math.hpp"
#include <array>
namespace mikudancestudio::editor {
// Original x64 command 300: readout uses truncated pi, but dialog acceptance
// uses the distinct 0x40490fe4 constant. Keep each operation float-rounded.
inline std::array<float, 7> CameraNumericValues(cpu_math::Vec3 position, cpu_math::Vec3 rotation,
                                                float distance) {
    return {position[0],
            position[1],
            position[2],
            float(rotation[0] / -3.141592f) * 180.f,
            float(rotation[1] / 3.141592f) * 180.f,
            float(rotation[2] / 3.141592f) * 180.f,
            -distance};
}
inline cpu_math::Vec3 CameraNumericRotation(const std::array<float, 7>& values) {
    constexpr float pi = 3.141595f;
    return {float(values[3] * -pi) / 180.f, float(values[4] * pi) / 180.f,
            float(values[5] * pi) / 180.f};
}
}
