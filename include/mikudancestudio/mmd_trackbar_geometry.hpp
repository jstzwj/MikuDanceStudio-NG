#pragma once
#include <algorithm>
#include <cstdint>

namespace mikudancestudio::ui {
struct TrackbarRectangle {
    int x, y, width, height;
};
struct TrackbarGeometry {
    TrackbarRectangle channel, thumb;
};
// Original horizontal common-control geometry at the editor's 96-DPI logical
// scale. The channel includes the thumb's end extents, rather than describing
// only the distance travelled by its center. Checked against TBM_GET*RECT.
inline TrackbarGeometry DialogTrackbarGeometry(int width, int height, bool ticks, int minimum,
                                               int maximum, int position) {
    const int thumbHeight = std::clamp(height - (ticks ? 7 : 4), 1, 19);
    const int thumbWidth = std::max(1, thumbHeight * 11 / 19);
    const int left = (thumbWidth + 1) / 2 + 2;
    const int right = std::max(left, width - thumbWidth / 2 - 3);
    const int travel = std::max(0, right - left - thumbWidth);
    const auto range = std::int64_t(maximum) - minimum;
    const auto value = std::clamp(std::int64_t(position) - minimum, std::int64_t(0),
                                  std::max(std::int64_t(0), range));
    const int offset = range > 0 ? int((value * travel + range / 2) / range) : 0;
    return {{left, (thumbHeight - 2) / 2, right - left, 4},
            {left + offset, 2, thumbWidth, thumbHeight}};
}
}
