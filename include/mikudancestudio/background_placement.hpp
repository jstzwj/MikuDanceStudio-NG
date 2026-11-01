#pragma once
#include <cstdint>

namespace mikudancestudio::render {
struct BackgroundPlacement {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    float scale = 1;
};
struct BackgroundView {
    std::int32_t left = 0, top = 0, width = 0, height = 0;
    float scale = 1;
    bool recording = false, multisampled = false;
    std::int32_t renderWidth = 0, surfaceWidth = 0, surfaceHeight = 0;
};
struct BackgroundRect {
    double left, top, right, bottom;
};
// Original x64 view scale: double arithmetic, then a float store.
inline float BackgroundViewScale(std::int32_t width) {
    return static_cast<float>(static_cast<double>(width) * 1.2 / 1280.0);
}
// Original x64 MediaAspect: preserve single-precision operation order and
// integer half-height / truncation. Fit to width, even when height overflows.
inline BackgroundPlacement FitBackground(std::int32_t width, std::int32_t height,
                                         const BackgroundView& view) {
    BackgroundPlacement p;
    p.width = width;
    p.height = height;
    const float span = static_cast<float>(view.width) / view.scale;
    p.scale = (span * 1.2f) / static_cast<float>(width);
    const float drawnHeight = (static_cast<float>(height) * p.scale * view.scale) / 1.2f;
    p.y = static_cast<std::int32_t>(static_cast<float>(view.height / 2) - drawnHeight * .5f);
    return p;
}
// Original x64 RVA 68CB0: every intermediate is rounded to float. The portable
// editor follows the x64 baseline regardless of the host CPU architecture.
inline BackgroundRect PlaceBackground(const BackgroundPlacement& p, const BackgroundView& view) {
    float sx = 1, sy = 1;
    if (view.recording) {
        sx = static_cast<float>(view.multisampled ? view.renderWidth : view.surfaceWidth) /
             static_cast<float>(view.width);
        sy = view.multisampled
                 ? sx
                 : static_cast<float>(view.surfaceHeight) / static_cast<float>(view.height);
    }
    float left = static_cast<float>(p.x) * view.scale;
    float top = static_cast<float>(p.y) * view.scale;
    if (view.recording) {
        left = left * sx;
        top = top * sy;
    }
    left = left / 1.2f;
    top = top / 1.2f;
    if (!view.recording) {
        left = left + static_cast<float>(view.left);
        top = top + static_cast<float>(view.top);
    }
    left = left - 1.f;
    top = top - 1.f;
    float right = static_cast<float>(p.width) * p.scale;
    float bottom = static_cast<float>(p.height) * p.scale;
    right = right * view.scale;
    bottom = bottom * view.scale;
    if (view.recording) {
        right = right * sx;
        bottom = bottom * sy;
    }
    right = right / 1.2f;
    bottom = bottom / 1.2f;
    right = right + left;
    bottom = bottom + top;
    return {left, top, right + 1.f, bottom + 1.f};
}
// Earlier x86 x87 placement, retained only for that compatibility host.
inline BackgroundRect PlaceBackgroundX86(const BackgroundPlacement& p, const BackgroundView& view) {
    constexpr double divisor = 1.2000000476837158;
    const double ratio = view.scale, zoom = p.scale;
    double sx = 1, sy = 1;
    if (view.recording) {
        sx = static_cast<float>(
            static_cast<double>(view.multisampled ? view.renderWidth : view.surfaceWidth) /
            view.width);
        sy = view.multisampled
                 ? sx
                 : static_cast<float>(static_cast<double>(view.surfaceHeight) / view.height);
    }
    const double left = view.recording
                            ? sx * p.x * ratio / divisor - 1.0
                            : view.left + static_cast<double>(p.x) * ratio / divisor - 1.0;
    const double top = view.recording ? sy * p.y * ratio / divisor - 1.0
                                      : static_cast<double>(p.y) * ratio / divisor + view.top - 1.0;
    return {left, top, p.width * zoom * ratio * sx / divisor + left + 1.0,
            p.height * zoom * ratio * sy / divisor + top + 1.0};
}
}
