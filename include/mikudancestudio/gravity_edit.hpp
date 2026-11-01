#pragma once
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <cmath>
#include <limits>

namespace mikudancestudio::editor {
// Original x64 cvttss2si: finite values outside the signed range produce INT_MIN.
inline std::int32_t GravityInteger(float value) {
    return value >= 2147483648.f || value < -2147483648.f || !std::isfinite(value)
               ? std::numeric_limits<std::int32_t>::min()
               : std::int32_t(value);
}
inline bool EditGravityChannel(format::PmmEvaluatedGravity& gravity, unsigned channel,
                               float value) {
    if (channel > 4 || !std::isfinite(value))
        return false;
    if (channel == 4)
        gravity.noise = GravityInteger(value);
    else {
        if (channel == 0)
            gravity.acceleration = value;
        else
            gravity.direction[channel - 1] = value;
        if (gravity.direction[0] == 0 && gravity.direction[1] == 0 && gravity.direction[2] == 0)
            gravity.direction[1] = .1f;
    }
    return true;
}
}
