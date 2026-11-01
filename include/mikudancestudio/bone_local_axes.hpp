#pragma once
#include <cmath>
#include <cstring>

namespace mikudancestudio::cpu_bone {
// SJIS bone-name needles (.rdata 0x52B7F0..0x52B81C); memcmp lengths in the
// original include the NUL byte.
inline constexpr char kNeedleStrstr[] = "\x8E\x77";           // 0x52B7F0
inline constexpr char kNameR1[] = "\x89\x45\x8E\xE8\x8E\xF1"; // 0x52B7F4 (7)
inline constexpr char kNameR2[] = "\x89\x45\x82\xD0\x82\xB6"; // 0x52B7FC (7)
inline constexpr char kNameR3[] = "\x89\x45\x98\x72";         // 0x52B804 (5)
inline constexpr char kNameL1[] = "\x8D\xB6\x8E\xE8\x8E\xF1"; // 0x52B80C (7)
inline constexpr char kNameL2[] = "\x8D\xB6\x82\xD0\x82\xB6"; // 0x52B814 (7)
inline constexpr char kNameL3[] = "\x8D\xB6\x98\x72";         // 0x52B81C (5)

template <class Model, class Normalize>
void LocalAxes(const Model* model, float out[16], Normalize normalize) {
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;
    out[0] = out[5] = out[10] = out[15] = 1.0f;

    if (model == nullptr || model->selectedBone < 0)
        return;
    auto* bones = model->boneTable;
    const auto& bone = bones[model->selectedBone];

    const bool hasAxes =
        !(bone.localAxes[0] == 0.0f && bone.localAxes[1] == 0.0f && bone.localAxes[2] == 0.0f);
    if (hasAxes && model->physicsMode == 2) {
        float a[3] = {bone.localAxes[0], bone.localAxes[1], bone.localAxes[2]};
        float b[3] = {bone.localAxes[3], bone.localAxes[4], bone.localAxes[5]};
        normalize(a);
        normalize(b);
        float c[3] = {b[1] * a[2] - b[2] * a[1], b[2] * a[0] - b[0] * a[2],
                      b[0] * a[1] - b[1] * a[0]};
        normalize(c);
        c[0] = -c[0];
        c[1] = -c[1];
        c[2] = -c[2];
        float e[3] = {c[2] * a[1] - c[1] * a[2], a[2] * c[0] - c[2] * a[0],
                      a[0] * c[1] - c[0] * a[1]};
        normalize(e);
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        out[4] = c[0];
        out[5] = c[1];
        out[6] = c[2];
        out[8] = e[0];
        out[9] = e[1];
        out[10] = e[2];
    } else {
        const char* name = bone.name;
        if (std::memcmp(name, kNameR1, 7) == 0 || std::memcmp(name, kNameR2, 7) == 0 ||
            std::memcmp(name, kNameR3, 5) == 0 || std::memcmp(name, kNameL1, 7) == 0 ||
            std::memcmp(name, kNameL2, 7) == 0 || std::memcmp(name, kNameL3, 5) == 0 ||
            std::strstr(name, kNeedleStrstr) != nullptr) {
            const auto& p = bones[bone.tailBone];
            const float dx = p.position[0] - bone.position[0];
            const float dy = p.position[1] - bone.position[1];
            const float len = std::sqrt(dx * dx + dy * dy);
            out[0] = dx / len;
            out[1] = dy / len;
            out[4] = -out[1];
            out[5] = out[0];
        }
    }
}

}
