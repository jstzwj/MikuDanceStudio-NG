#pragma once
#include <cstdint>

namespace mikudancestudio::keyframe_edit {
template <class Key> void ResetBoneCurve(Key& key, unsigned lane) {
    key.interpolation[lane] = key.interpolation[lane + 4] = 20;
    key.interpolation[lane + 8] = key.interpolation[lane + 12] = 107;
}

// Native automatic smoothing (x64 RVA C9070). The caller supplies a validated
// indexed track. Neighbor control points change too; record identity matters.
template <class Lookup>
void RebuildBoneCurve(Lookup&& at, std::uint32_t roots, std::uint32_t index, unsigned lane) {
    auto& key = at(index);
    if (lane >= 3 || index < roots) {
        ResetBoneCurve(key, lane);
        return;
    }
    auto& previous = at(key.previous);
    const float value = key.position[lane];
    const float prior = previous.position[lane];
    if (value == prior) {
        ResetBoneCurve(key, lane);
        return;
    }
    if (key.previous < roots) {
        key.interpolation[lane] = 64;
        key.interpolation[lane + 4] = 0;
    } else {
        const float before = at(previous.previous).position[lane];
        const float left = prior - before, right = value - prior;
        const float slope = left * right;
        if (slope > 0) {
            previous.interpolation[lane + 8] = previous.interpolation[lane + 12] = 87;
            key.interpolation[lane] = key.interpolation[lane + 4] = 40;
        } else {
            previous.interpolation[lane + 8] = 64;
            previous.interpolation[lane + 12] = 127;
            key.interpolation[lane] = 64;
            key.interpolation[lane + 4] = 0;
        }
    }
    if (!key.next) {
        key.interpolation[lane + 8] = 64;
        key.interpolation[lane + 12] = 127;
        return;
    }
    auto& next = at(key.next);
    const float left = next.position[lane] - value, right = value - prior;
    const float slope = left * right;
    // The original COMISS/JBE also takes the nonpositive branch for unordered
    // products (possible when finite positions overflow during subtraction).
    if (slope > 0) {
        key.interpolation[lane + 8] = key.interpolation[lane + 12] = 87;
        next.interpolation[lane] = next.interpolation[lane + 4] = 40;
    } else {
        key.interpolation[lane + 8] = 64;
        key.interpolation[lane + 12] = 127;
        next.interpolation[lane] = 64;
        next.interpolation[lane + 4] = 0;
    }
}
}
