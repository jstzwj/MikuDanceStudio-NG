#pragma once
#include "cpu_bone_types.hpp"
#include <cstdint>

namespace mikudancestudio::cpu_bone {
struct ScreenPoint {
    int x = 393939, y = 393939;
    float clipW = 0;
};
// Both hosts supply their own storage. Selection is separate from pending pose
// edits, and picking/box selection deliberately have different eligibility.
template <class BoneAt, class SelectedAt>
bool Pick(std::uint32_t count, BoneAt boneAt, SelectedAt selectedAt, int& active, int x, int y,
          int physicsMode, bool additive) {
    int first = -1, candidate = -1;
    bool changed = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& bone = boneAt(i);
        if (!(bone.flags & kBoneFlagVisible) ||
            !(static_cast<std::int8_t>(bone.type) < static_cast<int>(BoneType::InertTip) ||
              bone.type == BoneType::FixedAxis))
            continue;
        if (bone.hasRigidBody && (physicsMode == 1 || (physicsMode == 2 && !bone.physicsDisabled)))
            continue;
        const auto dx = std::int64_t(bone.screenX) - x;
        const auto dy = std::int64_t(bone.screenY) - y;
        if (dx <= -8 || dx >= 8 || dy <= -8 || dy >= 8 || dx * dx + dy * dy >= 64)
            continue;
        if (additive) {
            selectedAt(i) = selectedAt(i) == 0 ? 1 : 0;
            if (selectedAt(i))
                active = int(i);
            else if (active == int(i))
                active = -1;
            changed = true;
        } else {
            if (first < 0)
                first = int(i);
            if (int(i) > active) {
                candidate = int(i);
                break;
            }
        }
    }
    if (!additive) {
        if (candidate < 0)
            candidate = first;
        if (candidate >= 0) {
            for (std::uint32_t i = 0; i < count; ++i)
                selectedAt(i) = i == unsigned(candidate);
            active = candidate;
            changed = true;
        }
    }
    return changed;
}
template <class BoneAt, class SelectedAt>
void Box(std::uint32_t count, BoneAt boneAt, SelectedAt selectedAt, int left, int top, int right,
         int bottom, bool additive) {
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& bone = boneAt(i);
        const bool eligible = static_cast<std::int8_t>(bone.type) <= int(BoneType::Effector) ||
                              bone.type == BoneType::FixedAxis;
        if (eligible && bone.screenX > left && bone.screenX < right && bone.screenY > top &&
            bone.screenY < bottom)
            selectedAt(i) = 1;
        else if (!additive)
            selectedAt(i) = 0;
    }
}
template <class SelectedAt>
void FinishBox(std::uint32_t count, SelectedAt selectedAt, int& active, bool additive) {
    if (!count)
        return;
    if (active < 0 || std::uint32_t(active) >= count || !selectedAt(unsigned(active))) {
        if (!additive)
            active = -1;
        if (selectedAt(0))
            active = 0;
    }
    int selected = 0, last = -1;
    for (std::uint32_t i = 0; i < count; ++i)
        if (selectedAt(i)) {
            ++selected;
            last = int(i);
        }
    if (selected == 1)
        active = last;
}
}
