#pragma once
#include "mikudancestudio/camera_key_math.hpp"
#include <array>
namespace mikudancestudio::keyframe_edit {
// Original x64 9D120 uses two single-precision operations, with an identity
// fast path. Keep the operand order and retain signed-zero payloads on identity.
template <class Key> void ScaleMorphKey(Key& key, float scale, float add) {
    if (key.allocated && (scale != 1.f || add != 0.f))
        key.value = static_cast<float>(scale * key.value) + add;
}
// Eight pairs: position XYZ, rotation XYZ in degrees, distance, integer FOV.
// Original x64 9C330 converts angle offsets with float pi (X is negated).
template <class Key> void ScaleCameraKey(Key& key, const std::array<float, 16>& values) {
    if (!key.selected)
        return;
    for (unsigned pair = 0; pair < 8; ++pair) {
        const float scale = values[2 * pair];
        float add = values[2 * pair + 1];
        if (pair >= 3 && pair <= 5)
            add = static_cast<float>(add * (pair == 3 ? -3.141592f : 3.141592f)) / 180.f;
        if (scale == 1.f && add == 0.f)
            continue;
        if (pair == 7) {
            key.fov = CameraFovInteger(
                static_cast<float>(static_cast<float>(SignedCameraFov(std::uint32_t(key.fov))) *
                                   scale) +
                add);
        } else {
            float& value = pair < 3   ? key.eye[pair]
                           : pair < 6 ? key.target[pair - 3]
                                      : key.distance;
            value = static_cast<float>(scale * value) + add;
        }
    }
}
}
