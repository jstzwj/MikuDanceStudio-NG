#pragma once
#include "cpu_bone_types.hpp"
#include <array>
#include <cmath>
#include <algorithm>
namespace mikudancestudio::cpu_bone {
inline bool OperationAxisActive(BoneType type, unsigned flags, int operation, int mode) {
    return type != BoneType::FixedAxis &&
           !(type == BoneType::UnderIk && (flags & kBoneFlagFixedAxis)) &&
           ((operation >= 9 && operation <= 14) || (mode >= 4 && mode <= 7) ||
            (mode >= 10 && mode <= 12));
}
// Shared native operation-mesh transform. The host retains its matrix arithmetic.
// The world matrix includes the editor's frame transform; the GPU view stays explicit.
template <class Bone, class Axes, class Multiply>
bool OperationAxis(const Bone& bone, int operation, int mode, int coordinate,
                   const std::array<float, 16>& frame, const std::array<float, 3>& eye, float fov,
                   Axes axes, Multiply multiply, std::array<float, 16>& output) {
    if (!OperationAxisActive(bone.type, bone.flags, operation, mode))
        return false;
    const auto identity = [] {
        return std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };
    auto translation = identity(), skin = identity();
    std::copy_n(bone.matInit, 16, skin.begin());
    std::copy_n(bone.position, 3, translation.begin() + 12);
    auto world = identity();
    if (coordinate == 1) {
        const auto placed = multiply(translation, skin);
        std::copy_n(placed.begin() + 12, 3, world.begin() + 12);
    } else {
        std::array<float, 16> basis;
        axes(basis.data());
        world = multiply(multiply(basis, translation), skin);
    }
    world = multiply(world, frame);
    float point[4];
    for (int c = 0; c < 4; ++c)
        point[c] = bone.matInit[c] * bone.position[0] + bone.matInit[4 + c] * bone.position[1] +
                   bone.matInit[8 + c] * bone.position[2] + bone.matInit[12 + c];
    float delta[3];
    for (int c = 0; c < 3; ++c)
        delta[c] = point[0] * frame[c] + point[1] * frame[4 + c] + point[2] * frame[8 + c] +
                   point[3] * frame[12 + c] - eye[c];
    const float scale =
        std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]) * (fov * .001f);
    auto scaling = identity();
    scaling[0] = scaling[5] = scaling[10] = scale;
    output = multiply(scaling, world);
    return true;
}
}
