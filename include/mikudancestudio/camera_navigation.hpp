#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mikudancestudio::navigation {
// x64 MMD pump arithmetic. The Windows host supplies D3DX's camera rotation;
// portable callers use the independently checked row-vector math boundary.
inline void RightDrag(float* angles, float& distance, float* offsets, bool modelMode, int dx,
                      int dy, bool shift, bool control) {
    if (modelMode && shift) {
        offsets[1] = offsets[1] + float(dy) * .1f;
        offsets[0] = offsets[0] - float(dx) * .1f;
    } else if (control) {
        distance = distance - float(dy) * .1f;
    } else {
        angles[1] = angles[1] - float(dx) * .01f;
        angles[0] = angles[0] - float(dy) * .01f;
    }
}
inline void PanOffsets(float* offsets, int dx, int dy, bool shift, bool control) {
    const float scale = shift ? .5f : control ? .005f : .05f;
    offsets[1] = offsets[1] + float(dy) * scale;
    offsets[0] = offsets[0] - float(dx) * scale;
}
inline void PanParentedCamera(float* position, float fov, int dx, int dy, bool shift,
                              bool control) {
    const float scale = shift ? 2e-4f : control ? 2e-6f : 2e-5f;
    const float depth = std::fabs(position[2]);
    const float factor = depth <= .3f ? 10.f : std::max(depth, 1.f);
    position[0] -= float(dx) * scale * factor * fov;
    position[1] += float(dy) * scale * factor * fov;
}
inline void PanFreeCamera(float* position, const float* rotation, int dx, int dy, bool shift,
                          bool control) {
    const float scale = shift ? .5f : control ? .005f : .05f;
    const float x = -float(dx), y = float(dy);
    position[0] += x * rotation[0] * scale + y * rotation[4] * scale + rotation[12];
    position[1] += x * rotation[1] * scale + y * rotation[5] * scale + rotation[13];
    position[2] -= x * rotation[2] * scale + y * rotation[6] * scale + rotation[14];
}
inline void Wheel(float* position, float& distance, int delta, bool cameraMode, int parentModel) {
    const auto wheel = static_cast<std::int16_t>(delta);
    if (cameraMode && parentModel >= 0)
        position[2] = float(wheel) * .05f + position[2];
    else
        distance = float(wheel) * .05f + distance;
}
}
