#include "mikudancestudio/background_placement.hpp"
#include "../src/render/background_quad.hpp"
#include <array>
#include <cstdio>

int main() {
    using namespace mikudancestudio;
    std::uint64_t hash = 14695981039346656037ull;
    unsigned count = 0;
    for (int mode : {0, 1, 2})
        for (int width : {127, 511, 1280, 1920})
            for (float scale : {.125f, 1.f, 2.75f})
                for (int x : {-257, 0, 91}) {
                    const render::BackgroundPlacement p{x, -x / 3, 79, 43, scale};
                    render::BackgroundView view;
                    view.left = 301;
                    view.top = 47;
                    view.width = width;
                    view.height = width * 3 / 4;
                    view.scale = render::BackgroundViewScale(width);
                    view.recording = mode != 0;
                    view.multisampled = mode == 1;
                    view.renderWidth = 1001;
                    view.surfaceWidth = 987;
                    view.surfaceHeight = 633;
                    const auto rect = render::PlaceBackground(p, view);
                    std::array<TexturedScreenVertex, 6> quad;
                    WriteBackgroundQuad(quad.data(), rect.left, rect.top, rect.right, rect.bottom);
                    const auto* bytes = reinterpret_cast<const std::uint8_t*>(quad.data());
                    for (std::size_t n = 0; n < sizeof quad; ++n)
                        hash = (hash ^ bytes[n]) * 1099511628211ull;
                    ++count;
                }
    // Frozen from MikuMikuDanceE_v932x64 RVA 68CB0, via the independent
    // background_placement_oracle_test. Do not regenerate from this helper.
    if (count != 108 || hash != 0x329bb94de05371caull) {
        std::fprintf(stderr, "background geometry: %u cases, hash=%016llx\n", count,
                     static_cast<unsigned long long>(hash));
        return 1;
    }
    std::puts("108 background rectangles match the original x64 vertex-byte digest");
    return 0;
}
