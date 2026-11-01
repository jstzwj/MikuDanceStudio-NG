#include "background_image_fixture.hpp"
#include <cstdio>
int main() {
    using namespace mikudancestudio::rhi;
    for (const auto& test : kBackgroundImages) {
        const auto source = BackgroundImage(test);
        RgbaImage prepared;
        if (!PrepareBackgroundImage(source, prepared) || prepared.width != 1024 ||
            prepared.height != 1024 || BackgroundImageHash(prepared) != test.nativeHash) {
            std::fprintf(stderr, "native filter mismatch: %ux%u pattern=%u hash=%016llx\n",
                         test.width, test.height, test.pattern,
                         static_cast<unsigned long long>(BackgroundImageHash(prepared)));
            return 1;
        }
    }
    auto alias = BackgroundImage(kBackgroundImages[3]);
    if (!PrepareBackgroundImage(alias, alias) ||
        BackgroundImageHash(alias) != kBackgroundImages[3].nativeHash)
        return 2;
    auto bad = BackgroundImage(kBackgroundImages[3]);
    bad.pixels.pop_back();
    if (PrepareBackgroundImage(bad, alias) || alias.width || alias.height || !alias.pixels.empty())
        return 3;
    // Earlier public counterexample: constant 45 with one green impulse.
    // Digest independently captured from D3DXCreateTextureFromFileInMemoryEx.
    RgbaImage boundary;
    boundary.width = boundary.height = 106;
    boundary.pixels.resize(106 * 106 * 4);
    for (unsigned i = 0; i < 106 * 106; ++i) {
        boundary.pixels[i * 4] = boundary.pixels[i * 4 + 1] = boundary.pixels[i * 4 + 2] = 45;
        boundary.pixels[i * 4 + 3] = 255;
    }
    boundary.pixels[(39 * 106 + 44) * 4 + 1] = 46;
    if (!PrepareTriangleDitherWrap(boundary, 128, 128, alias) ||
        BackgroundImageHash(alias) != 0xa18602af88fd2325ull)
        return 4;
    std::puts("22 generated RGBA backgrounds match frozen native D3DX9_43 output exactly");
    return 0;
}
