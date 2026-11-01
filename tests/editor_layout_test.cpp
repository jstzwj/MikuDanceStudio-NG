#include "mikudancestudio/mmd_editor_layout.hpp"
#include "mikudancestudio/sidebar_navigation.hpp"
#include "editor_layout_reference.hpp"
#include <cstdio>
using namespace mikudancestudio;
int main() {
    if (!ui::SidebarHit(250, 542, 250, 700) || !ui::SidebarHit(256, 542, 250, 700) ||
        ui::SidebarHit(249, 542, 250, 700) || ui::SidebarHit(257, 542, 250, 700) ||
        ui::SidebarHit(250, 543, 250, 700))
        return 1;
    if (ui::DragSidebar(-100, 1024).width != 250 || ui::DragSidebar(5000, 1024).width != 1019 ||
        ui::DragSidebar(381, 1024).width != 381 ||
        ui::DragSidebar(381, 1024).ratio != .3720703125f ||
        ui::ResizeSidebar(.29296875f, 1280) != 375 || ui::ResizeSidebar(.29296875f, 640) != 250)
        return 1;
    if (ui::ResizeSidebar(.7f, 1000) != 700 || ui::ResizeSidebar<true>(.7f, 1000) != 699)
        return 1;
    for (const auto& reference : kOriginalLayouts) {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned flags = 0; flags < 128; ++flags) {
            ui::EditorLayoutOptions options{bool(flags & 1), bool(flags & 2),  bool(flags & 4),
                                            bool(flags & 8), bool(flags & 16), bool(flags & 32),
                                            bool(flags & 64)};
            const auto controls =
                ui::MmdEditorControls(reference.sidebar, reference.bottom, options);
            if (controls.size() != 146)
                return 1;
            for (const auto& r : controls)
                for (auto value : {r.id, r.x, r.y, r.width, r.height})
                    for (unsigned shift = 0; shift < 32; shift += 8)
                        hash =
                            (hash ^ std::uint8_t(std::uint32_t(value) >> shift)) * 1099511628211ull;
        }
        if (hash != reference.hash) {
            std::fprintf(stderr, "original layout mismatch: sidebar=%d bottom=%d hash=%016llx\n",
                         reference.sidebar, reference.bottom,
                         static_cast<unsigned long long>(hash));
            return 1;
        }
    }
    for (bool separate : {false, true}) {
        std::uint64_t hash = 14695981039346656037ull;
        for (int top : {0, 25, 83})
            for (int bottom : {480, 777})
                for (int boundary : kViewportFitBoundaries)
                    for (int delta : {-1, 0, 1}) {
                        const int sidebar = separate ? -9 : 250;
                        const int right = sidebar + 9 + boundary + delta;
                        const auto controls = ui::MmdViewportControls(
                            sidebar, right, {0, 0, top, right, bottom - top});
                        if (controls.size() != 22)
                            return 1;
                        for (const auto& r : controls)
                            for (auto value : {r.id, r.x, r.y, r.width, r.height})
                                for (unsigned shift = 0; shift < 32; shift += 8)
                                    hash = (hash ^ std::uint8_t(std::uint32_t(value) >> shift)) *
                                           1099511628211ull;
                    }
        if (hash != kOriginalViewportLayouts[separate])
            return 1;
    }
    std::puts("Original layout references: 1524 states and 128760 exact control rectangles passed");
    return 0;
}
