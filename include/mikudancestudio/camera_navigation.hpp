#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mikudancestudio::navigation {
// Values stored in the original image, including its rounded angle presets.
inline constexpr float HalfPi = 1.5707999467849731f; // 0x3FC90FF9
inline constexpr float Pi = 3.1415920257568359f;     // 0x40490FD8
struct ViewPreset {
    float pitch = 0, yaw = 0, distance = -45;
    bool referenceHeight = true, reload = false;
};
inline bool Preset(unsigned number, ViewPreset& out) {
    ViewPreset p;
    switch (number) {
    case 0:
        p.pitch = HalfPi;
        p.reload = true;
        break;
    case 1:
        p.pitch = HalfPi;
        p.distance = -30;
        p.referenceHeight = false;
        break;
    case 2:
        break;
    case 4:
        p.yaw = HalfPi;
        break;
    case 5:
        p.pitch = -HalfPi;
        p.referenceHeight = false;
        break;
    case 6:
        p.yaw = -HalfPi;
        break;
    case 8:
        p.yaw = Pi;
        break;
    default:
        return false;
    }
    out = p;
    return true;
}
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
// Native header hand control (x64 32A2B..32BEA). Its free-camera
// sensitivity differs from the middle-button path below.
inline void HeaderPanFreeCamera(float* position, const float* rotation, int dx, int dy, bool shift,
                                bool control) {
    const float scale = shift ? 1.f : control ? .01f : .1f;
    const float x = -float(dx), y = float(dy);
    position[0] += y * rotation[4] * scale + x * rotation[0] * scale + rotation[12];
    position[1] += y * rotation[5] * scale + x * rotation[1] * scale + rotation[13];
    position[2] -= y * rotation[6] * scale + x * rotation[2] * scale + rotation[14];
}
inline void HeaderZoom(float* position, float& distance, bool cameraMode, int parentModel, int dy,
                       bool shift, bool control) {
    float& target = cameraMode && parentModel >= 0 ? position[2] : distance;
    if (shift)
        target -= float(dy);
    else
        target -= float(dy) * (cameraMode && parentModel >= 0 && control ? .01f : .1f);
}
struct HeaderPictureState {
    std::int32_t x = 0, y = 0;
    float scale = 0;
};
// Native Ctrl+header manipulation is independent of picture visibility.
// Keep float stores and truncation order; reject out-of-range integer stores
// before publishing any field instead of invoking an overflowing C++ cast.
inline bool HeaderPictureDrag(HeaderPictureState& state, unsigned operation, int dx, int dy,
                              unsigned width, unsigned height, float viewScale) {
    if ((operation != 1 && operation != 2) || !width || !height || !std::isfinite(viewScale) ||
        viewScale <= 0)
        return false;
    auto next = state;
    float x, y;
    if (operation == 1) {
        next.scale -= float(dy) * 10.f / float(width) * viewScale;
        x = float(dy) * 5.f * viewScale;
        y = float(dy) * 5.f / float(width) * float(height) * viewScale;
    } else {
        x = float(dx) * 5.f * viewScale;
        y = float(dy) * 5.f * viewScale;
    }
    const auto add = [](std::int32_t& target, float value) {
        if (!std::isfinite(value) || double(value) < double(INT32_MIN) ||
            double(value) > double(INT32_MAX))
            return false;
        const auto result = std::int64_t(target) + std::int32_t(value);
        if (result < INT32_MIN || result > INT32_MAX)
            return false;
        target = std::int32_t(result);
        return true;
    };
    if (!std::isfinite(next.scale) || !add(next.x, x) || !add(next.y, y))
        return false;
    state = next;
    return true;
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
