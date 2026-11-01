#pragma once
#include "cpu_math.hpp"
#include <cmath>
namespace mikudancestudio::editor {
// MMD v9.32 x64 numeric bone readouts (PostViewRefresh) and the matching
// Z*X*Y edit commit. These signs differ from camera and file rotations.
inline cpu_math::Vec3 BoneRotationDegrees(const cpu_math::Quaternion& q) {
    const auto m = cpu_math::RotationQuaternion(q);
    float x = std::asin(-m[9]), y = std::atan2(m[8], m[10]), z = std::atan2(m[1], m[5]);
    if (std::fabs(std::cos(x)) < 1e-6f) {
        z = float(double(z) + (m[1] > 0 ? 3.141592f : -3.141592f));
        y = float(double(y) + (m[8] > 0 ? 3.141592f : -3.141592f));
    }
    if (std::fabs(x) < 1e-6f)
        x = 0;
    if (std::fabs(y) < 1e-6f)
        y = 0;
    if (std::fabs(z) < 1e-6f)
        z = 0;
    return {float(x / 3.141592f) * 180.f, float(y / -3.141592f) * 180.f,
            float(z / -3.141592f) * 180.f};
}
inline cpu_math::Quaternion BoneRotationFromDegrees(const cpu_math::Vec3& degrees) {
    constexpr double pi = 3.141592025756836;
    const auto x = float(double(degrees[0]) * pi / 180.),
               y = float(-double(degrees[1]) * pi / 180.),
               z = float(-double(degrees[2]) * pi / 180.);
    return cpu_math::QuaternionFromMatrix(
        cpu_math::Multiply(cpu_math::Multiply(cpu_math::RotationZ(z), cpu_math::RotationX(x)),
                           cpu_math::RotationY(y)));
}
}
