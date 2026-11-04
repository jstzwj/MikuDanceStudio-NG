#pragma once
#include "mikudancestudio/rhi_render_pass.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

// User-approved visual acceptance for complete editor scene composites only.
// Resource copies, self-shadow depth, repeat submissions and other numerical
// contracts retain their own exact/strict gates. Original images are unchanged.
namespace composite_visual {
struct Difference {
    double mean = 0, largePixelFraction = 0, blockMeanMaximum = 0;
    std::size_t alphaDifferences = 0;
    bool Accepted() const {
        return mean <= .5 && largePixelFraction <= .0005 && blockMeanMaximum <= 12;
    }
};
inline Difference Compare(const mikudancestudio::rhi::AttachmentReadback& a,
                          const mikudancestudio::rhi::AttachmentReadback& b) {
    using mikudancestudio::rhi::TextureFormat;
    const auto& d = a.description;
    if (!d.width || !d.height || d.width != b.description.width ||
        d.height != b.description.height || d.format != TextureFormat::Rgba8 ||
        b.description.format != d.format || a.bytes.size() != b.bytes.size() ||
        a.bytes.size() != std::size_t(d.width) * d.height * 4)
        throw std::runtime_error("composite visual readback shape/format mismatch");
    Difference result;
    for (std::size_t i = 3; i < a.bytes.size(); i += 4)
        result.alphaDifferences += a.bytes[i] != b.bytes[i];
    // Review transparent boundaries over both light and dark backgrounds;
    // dropping Alpha or comparing only RGB would conceal compositing defects.
    for (const double background : {0., 255.}) {
        double errorSum = 0, blockMaximum = 0;
        std::size_t largePixels = 0;
        for (unsigned y0 = 0; y0 < d.height; y0 += 8)
            for (unsigned x0 = 0; x0 < d.width; x0 += 8) {
                std::array<double, 3> block{};
                unsigned count = 0;
                for (unsigned y = y0; y < std::min(y0 + 8, d.height); ++y)
                    for (unsigned x = x0; x < std::min(x0 + 8, d.width); ++x) {
                        const auto i = (std::size_t(y) * d.width + x) * 4;
                        double maximum = 0;
                        for (unsigned c = 0; c < 3; ++c) {
                            const auto color = [&](const auto& bytes) {
                                return (bytes[i + c] * double(bytes[i + 3]) +
                                        background * (255 - bytes[i + 3])) /
                                       255;
                            };
                            const auto delta = color(a.bytes) - color(b.bytes);
                            errorSum += std::abs(delta);
                            maximum = std::max(maximum, std::abs(delta));
                            block[c] += delta;
                        }
                        largePixels += maximum > 3;
                        ++count;
                    }
                for (const auto value : block)
                    blockMaximum = std::max(blockMaximum, std::abs(value) / count);
            }
        const auto pixels = double(d.width) * d.height;
        result.mean = std::max(result.mean, errorSum / (pixels * 3));
        result.largePixelFraction = std::max(result.largePixelFraction, largePixels / pixels);
        result.blockMeanMaximum = std::max(result.blockMeanMaximum, blockMaximum);
    }
    return result;
}
}
