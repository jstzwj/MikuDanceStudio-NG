#pragma once
#include <cstdint>
#include <array>

namespace mikudancestudio::render {
struct ViewportTools {
    bool camera = true, playing = false;
    int coordinateSystem = 0, boneMode = 0, operation = 0, editMode = 0, dragMode = 0;
    int centerX = 393939, centerY = 393939;
    bool selectionBox = false;
    std::array<int, 4> box{}; // Physical pixel left/top/right/bottom.
};
// The header uses the host client edge, which can extend past the aspect-fitted
// viewport. Hosts apply their UI DPI transform to these logical rectangles.
inline int ViewportHeaderToolAt(int x, int y, int right, int viewportTop) {
    if (y > viewportTop - 25 && y < viewportTop) {
        if (x > right - 60 && x < right - 36)
            return 1;
        if (x > right - 30 && x < right - 6)
            return 2;
    }
    return 0;
}
inline std::array<int, 2> ViewportHeaderRestorePoint(int operation, int right, int viewportTop) {
    return {right - (operation == 1 ? 48 : 18), viewportTop - 12};
}
// Pixel coordinates of the native operation-cell centers on pointer restoration.
inline std::array<int, 2> ViewportToolRestorePoint(int operation, int right, int bottom,
                                                   float scale) {
    if (operation < 9 || operation > 20)
        return {right / 2, bottom / 2};
    const int column = (operation - 9) % 3;
    const bool upper = operation <= 11 || operation >= 18;
    return {right - int(std::array{110.f, 70.f, 30.f}[column] * scale),
            bottom - int((upper ? 65.f : 25.f) * scale)};
}
template <class Append> void AppendSelectionBox(int x0, int y0, int x1, int y1, Append append) {
    const float left = float(x0 < x1 ? x0 : x1), right = float(x0 < x1 ? x1 : x0);
    const float top = float(y0 < y1 ? y0 : y1), bottom = float(y0 < y1 ? y1 : y0);
    append(left, top, left, bottom, 0xFFFFFFFFu);
    append(left, bottom, right, bottom, 0xFFFFFFFFu);
    append(right, bottom, right, top, 0xFFFFFFFFu);
    append(right, top, left, top, 0xFFFFFFFFu);
}
// The native operation-panel atlas geometry, independent of window or GPU.
template <class Append>
void AppendViewportTools(const ViewportTools& state, float right, float bottom, float scale,
                         Append append) {
    // 0x46C414..0x46C6C5: global/local target strip. All three variants
    // occupy the same 120x30 rectangle and select a vertical atlas band.
    const bool playback = state.playing;
    if (!playback) {
        float targetV0 = 0.470705f;
        float targetV1 = 0.52734601f;
        const int target = state.coordinateSystem;
        if (target == 1) {
            targetV0 = 0.52929902f;
            targetV1 = 0.58594f;
        } else if (target == 2) {
            targetV0 = 0.58789301f;
            targetV1 = 0.64453399f;
        }
        append(right - scale * 130.0f, bottom - scale * 115.0f, right - scale * 10.0f,
               bottom - scale * 85.0f, 0.001953f, targetV0, 0.23242199f, targetV1);
    }

    // 0x46C6E2..0x46CE84: camera and bone operation rows. Playback takes
    // LABEL_105 and deliberately emits neither rows nor highlights.
    if (!playback) {
        if (state.camera) {
            append(right - scale * 130.0f, bottom - scale * 80.0f, right - scale * 10.0f,
                   bottom - scale * 50.0f, 0.001953f, 0.41210899f, 0.23242199f, 0.46875f);
            append(right - scale * 130.0f, bottom - scale * 40.0f, right - scale * 10.0f,
                   bottom - scale * 10.0f, 0.001953f, 0.177734f, 0.23242199f, 0.234375f);
        } else {
            const int boneMode = state.boneMode;
            if (boneMode >= 0 && boneMode <= 3) {
                const bool alternate = (boneMode & 1) != 0;
                const bool lowerPair = boneMode >= 2;
                append(right - scale * 130.0f, bottom - scale * 80.0f, right - scale * 10.0f,
                       bottom - scale * 50.0f, 0.001953f, lowerPair ? 0.35351601f : 0.29492199f,
                       0.23242199f, lowerPair ? 0.41015601f : 0.35156301f);
                append(right - scale * 130.0f, bottom - scale * 40.0f, right - scale * 10.0f,
                       bottom - scale * 10.0f, 0.001953f, alternate ? 0.119141f : 0.060547002f,
                       0.23242199f, alternate ? 0.175781f : 0.117188f);
            }
        }

        // 0x46CF9B..0x46D71F: the six 40x30 operation highlights. The
        // paired interaction modes share one atlas cell exactly as below.
        const int operation = state.operation;
        const auto highlight = [&](int first, int second, int column, int row) {
            if (operation != first && operation != second)
                return;
            const float x0 = right - scale * (130.0f - 40.0f * column);
            const float y0 = bottom - scale * (80.0f - 40.0f * row);
            const float u0 = 0.001953f + 0.078125f * column;
            const float v0 = row == 0 ? 0.23632801f : 0.001953f;
            append(x0, y0, x0 + scale * 40.0f, y0 + scale * 30.0f, u0, v0,
                   column == 2 ? 0.23242199f : u0 + 0.078125f, row == 0 ? 0.29296899f : 0.058594f);
        };
        highlight(9, 18, 0, 0);
        highlight(10, 19, 1, 0);
        highlight(11, 20, 2, 0);
        highlight(12, 15, 0, 1);
        highlight(13, 16, 1, 1);
        highlight(14, 17, 2, 1);

        // 0x46D74D..0x46DB2A: operation cursor pictures in bone mode. These
        // use raw projected mouse coordinates returned by 0x499BD0 and are
        // intentionally not multiplied by the viewport scale.
        if (!state.camera) {
            const float x = static_cast<float>(state.centerX);
            const float y = static_cast<float>(state.centerY);
            const int cursorKind = state.editMode;
            if (cursorKind == 3) {
                float u0 = 0.5f;
                float v0 = 0.5f;
                if (operation == 3) {
                    v0 = 0.75f;
                } else if (operation == 4) {
                    u0 = 0.75f;
                } else if (operation == 5) {
                    u0 = 0.75f;
                    v0 = 0.75f;
                }
                append(x - 45.0f, y - 45.0f, x + 45.0f, y + 45.0f, u0, v0, u0 + 0.25f, v0 + 0.25f);
            } else if (cursorKind == 4) {
                float u0 = 0.5f;
                float v0 = 0.0f;
                if (operation == 6) {
                    v0 = 0.25f;
                } else if (operation == 7) {
                    u0 = 0.75f;
                } else if (operation == 8) {
                    u0 = 0.75f;
                    v0 = 0.25f;
                }
                append(x - 13.0f, y - 67.0f, x + 62.0f, y + 8.0f, u0, v0, u0 + 0.25f, v0 + 0.25f);
            }
        }
    }
}
// Strict native hit rectangles. Header orbit/pan controls are supplied by
// the host; this function addresses the drawable viewport only.
inline int ViewportToolAt(const ViewportTools& state, int x, int y, float right, float bottom,
                          float scale, bool dragging = false) {
    int operation = 0;
    if (!state.playing) {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float x0 = right - 127.0f * scale;
        const float x1 = right - 92.0f * scale;
        const float x2 = right - 86.0f * scale;
        const float x3 = right - 51.0f * scale;
        const float x4 = right - 48.0f * scale;
        const float x5 = right - 13.0f * scale;
        const float upperTop = bottom - 80.0f * scale;
        const float upperBottom = bottom - 50.0f * scale;
        const float lowerTop = bottom - 40.0f * scale;
        const float lowerBottom = bottom - 10.0f * scale;
        int column = -1;
        if (fx > x0 && fx < x1)
            column = 0;
        else if (fx > x2 && fx < x3)
            column = 1;
        else if (fx > x4 && fx < x5)
            column = 2;
        if (column >= 0) {
            const bool modelPanel = state.camera;
            const std::uint32_t axisMode = state.boneMode;
            if (fy > upperTop && fy < upperBottom) {
                if (modelPanel)
                    operation = std::array{18, 19, 20}[column];
                else if (axisMode == 0 || axisMode == 1)
                    operation = std::array{9, 10, 11}[column];
            } else if (fy > lowerTop && fy < lowerBottom) {
                if (modelPanel)
                    operation = std::array{15, 16, 17}[column];
                else if (axisMode == 0 || axisMode == 2)
                    operation = std::array{12, 13, 14}[column];
            }
        }

        if (operation == 0 && state.editMode == 3) {
            const int cx = state.centerX;
            const int cy = state.centerY;
            const auto px = std::int64_t(x) - cx;
            const auto py = std::int64_t(y) - cy;
            const bool horizontal =
                ((px > 7 && px < 35) || (px > -35 && px < -7)) && py > -7 && py < 7;
            const bool vertical =
                ((py > 7 && py < 35) || (py > -35 && py < -7)) && px > -7 && px < 7;
            if (horizontal)
                operation = 3;
            else if (vertical)
                operation = 4;
            else if (px > -46 && px < 46 && py > -46 && py < 46) {
                const auto radius2 = px * px + py * py;
                if (radius2 > 1225 && radius2 < 2025)
                    operation = 5;
            }
        } else if (operation == 0 && state.editMode == 4) {
            const int px = x - state.centerX;
            const int py = y - state.centerY;
            if (px > 41 && px < 61 && py > -8 && py < 8)
                operation = 6;
            else if (px > -13 && px < 8 && py > -61 && py < -42)
                operation = 7;
            else if (px > -13 && px < 11 && py > -16 && py < 8)
                operation = 8;
        }

        if (operation == 0 && !dragging) {
            const float coordLeft = right - 120.0f * scale;
            const float coordTop = bottom - 105.0f * scale;
            if (fx > coordLeft && fx < coordLeft + 25.0f * scale && fy > coordTop &&
                fy < coordTop + 25.0f * scale)
                operation = 21;
        }
    }

    return operation;
}
}
