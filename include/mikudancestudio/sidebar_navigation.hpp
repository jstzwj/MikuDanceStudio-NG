#pragma once
#include <algorithm>
namespace mikudancestudio::ui {
inline bool SidebarHit(int x, int y, int sidebar, int bottom) {
    return sidebar <= x && x <= sidebar + 6 && y <= bottom - 158;
}
struct SidebarPlacement {
    int width;
    float ratio;
};
inline SidebarPlacement DragSidebar(int x, int clientWidth) {
    const int width = std::min(std::max(x, 250), clientWidth - 5);
    return {width, float(double(width) / double(clientWidth))};
}
// Original x64 rounds the multiply to binary32; its x86 implementation uses
// the explicit double intermediate retained by the compatibility host.
template <bool NativeX86 = false> int ResizeSidebar(float ratio, int clientWidth) {
    const int width =
        NativeX86 ? int(double(ratio) * clientWidth) : int(ratio * float(clientWidth));
    return std::max(250, width);
}
}
