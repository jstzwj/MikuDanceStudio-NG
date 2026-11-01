#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

namespace mikudancestudio::editor {
// MMD reset (x64 RVA 40248..405AA). The caller visits selected bones in model
// order, so a selected root is reset before an unparented bone reads it.
// The x86 adapter explicitly instantiates its legacy double arithmetic.
template <class Scalar = float>
void ResetBonePose(unsigned index, int parent, const float* rest, const float* matrix,
                   const float* rootPosition, const float* rootRotation, float* position,
                   float* rotation, std::uint8_t& edited) {
    if (index == 0) {
        position[1] = 0;
        std::fill_n(rotation, 3, 0.f);
        rotation[3] = 1;
        return; // Root reset does not set the per-bone edit flag.
    }
    std::array<Scalar, 3> target{};
    std::array<float, 4> quaternion{0, 0, 0, 1};
    if (parent == -1) {
        for (unsigned c = 0; c < 3; ++c) {
            if constexpr (std::is_same_v<Scalar, float>) {
                target[c] = Scalar(rest[0]) * matrix[c];
                target[c] += Scalar(rest[1]) * matrix[4 + c];
            } else {
                target[c] = Scalar(rest[1]) * matrix[4 + c];
                target[c] += Scalar(rest[0]) * matrix[c];
            }
            target[c] += Scalar(rest[2]) * matrix[8 + c];
            target[c] += matrix[12 + c];
            target[c] -= rest[c];
            target[c] += rootPosition[c];
        }
        std::copy_n(rootRotation, 4, quaternion.begin());
    }
    bool changed = false;
    for (unsigned c = 0; c < 3; ++c)
        changed |= position[c] != target[c];
    for (unsigned c = 0; c < 4; ++c)
        changed |= rotation[c] != quaternion[c];
    if (changed) {
        std::copy(target.begin(), target.end(), position);
        std::copy(quaternion.begin(), quaternion.end(), rotation);
        edited = 1;
    }
}
}
