#pragma once
#include "mikudancestudio/cpu_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace mikudancestudio::keyframe_edit {
// Original x64 9C9C0. Rotation edits decompose and rebuild Rz * Rx * Ry;
// they do not normalize the incoming quaternion or wrap the resulting angles.
template <class Key> void ScaleBoneKey(Key& key, const std::array<float, 12>& values) {
    if (!key.allocated)
        return;
    std::array<float, 6> add;
    for (unsigned axis = 0; axis < 6; ++axis) {
        add[axis] = values[2 * axis + 1];
        if (axis >= 3)
            add[axis] = float(add[axis] * (axis == 3 ? 3.141592f : -3.141592f)) / 180.f;
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        if (values[2 * axis] != 1.f || add[axis] != 0.f)
            key.position[axis] = float(values[2 * axis] * key.position[axis]) + add[axis];
    if (values[6] == 1.f && values[8] == 1.f && values[10] == 1.f && add[3] == 0.f &&
        add[4] == 0.f && add[5] == 0.f)
        return;
    cpu_math::Quaternion q;
    std::copy_n(std::begin(key.rotation), 4, q.begin());
    const auto m = cpu_math::RotationQuaternion(q);
    float z = std::atan2(m[1], m[5]), x = std::asin(-m[9]), y = std::atan2(m[8], m[10]);
    if (std::fabs(std::cos(x)) < 1e-6f) {
        z += m[1] > 0.f ? 3.141592f : -3.141592f;
        y += m[8] > 0.f ? 3.141592f : -3.141592f;
    }
    const auto rotation =
        cpu_math::Multiply(cpu_math::Multiply(cpu_math::RotationZ(float(z * values[10]) + add[5]),
                                              cpu_math::RotationX(float(x * values[6]) + add[3])),
                           cpu_math::RotationY(float(y * values[8]) + add[4]));
    q = cpu_math::QuaternionFromMatrix(rotation);
    std::copy(q.begin(), q.end(), std::begin(key.rotation));
}
}
