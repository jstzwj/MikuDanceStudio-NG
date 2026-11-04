#include "composite_visual_compare.hpp"
#include <iostream>
using namespace mikudancestudio::rhi;
int main() {
    AttachmentReadback reference;
    reference.description = {512, 512, TextureFormat::Rgba8};
    reference.bytes.resize(512 * 512 * 4, 255);
    auto actual = reference;
    const auto accepted = [&](bool value) {
        return composite_visual::Compare(actual, reference).Accepted() == value;
    };
    if (!accepted(true))
        return 1;
    // Whole-frame tint and transparency regressions cannot hide in an area
    // average, including a white image composited over the dark background.
    for (std::size_t i = 0; i < actual.bytes.size(); i += 4)
        actual.bytes[i] = 250;
    if (!accepted(false))
        return 1;
    actual = reference;
    for (std::size_t i = 3; i < actual.bytes.size(); i += 4)
        actual.bytes[i] = 0;
    if (!accepted(false))
        return 1;
    // A small missing object also fails the local block limit, independently
    // of the allowed fraction of isolated edge pixels.
    actual = reference;
    for (unsigned y = 32; y < 40; ++y)
        for (unsigned x = 32; x < 40; ++x)
            for (unsigned c = 0; c < 3; ++c)
                actual.bytes[(y * 512 + x) * 4 + c] = 0;
    if (!accepted(false))
        return 1;
    actual = reference;
    actual.bytes[(32 * 512 + 32) * 4] = 180;
    if (!accepted(true))
        return 1;
    std::cout << "Visual scene gate rejects tint, lost Alpha and missing geometry\n";
}
