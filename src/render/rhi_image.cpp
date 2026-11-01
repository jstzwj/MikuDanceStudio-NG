#include "mikudancestudio/rhi_image.hpp"

#include <climits>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>

#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mikudancestudio::rhi {
namespace {
// True-color TGA, including RLE, with explicit raster bounds/origin. Keep
// unsupported palettes/bit depths separate rather than guess their semantics.
bool DecodeTrueColorTga(const std::uint8_t* source, std::size_t length, RgbaImage& output) {
    if (length < 18 || source[1] != 0 || (source[2] != 2 && source[2] != 10))
        return false;
    const unsigned width = source[12] | unsigned(source[13]) << 8,
                   height = source[14] | unsigned(source[15]) << 8;
    const unsigned bits = source[16], attributes = source[17] & 15;
    if (!width || !height || width > 4096 || height > 4096 || (bits != 24 && bits != 32) ||
        (source[17] & 0xc0) || (bits == 24 && attributes) ||
        (bits == 32 && attributes != 0 && attributes != 8))
        return false;
    const unsigned stride = bits / 8;
    std::size_t cursor = 18 + source[0], pixel = 0, total = std::size_t(width) * height;
    if (cursor > length)
        return false;
    RgbaImage result{width, height, std::vector<std::uint8_t>(total * 4)};
    const auto write = [&](const std::uint8_t* value) {
        auto x = pixel % width, y = pixel / width;
        if (source[17] & 0x10)
            x = width - 1 - x;
        if (!(source[17] & 0x20))
            y = height - 1 - y;
        auto* target = result.pixels.data() + (y * width + x) * 4;
        target[0] = value[2];
        target[1] = value[1];
        target[2] = value[0];
        target[3] = bits == 32 && attributes == 8 ? value[3] : 255;
        ++pixel;
    };
    while (pixel < total) {
        std::size_t count = 1;
        bool repeated = false;
        if (source[2] == 10) {
            if (cursor >= length)
                return false;
            const auto packet = source[cursor++];
            count = (packet & 127) + 1;
            repeated = (packet & 128) != 0;
        }
        if (count > total - pixel)
            return false;
        const auto bytes = (repeated ? 1 : count) * stride;
        if (bytes > length - cursor)
            return false;
        for (std::size_t n = 0; n < count; ++n)
            write(source + cursor + (repeated ? 0 : n * stride));
        cursor += bytes;
    }
    output = std::move(result);
    return true;
}
}
bool DecodeTextureRgba(const void* bytes, std::size_t length, RgbaImage& image) {
    image = {};
    if (!bytes || length == 0 || length > INT_MAX)
        return false;
    const auto* input = static_cast<const stbi_uc*>(bytes);
    const bool png = length >= 8 && std::memcmp(input, "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool bmp = length >= 54 && input[0] == 'B' && input[1] == 'M';
    if (!png && !bmp)
        return DecodeTrueColorTga(input, length, image);
    if (bmp) {
        const auto u32 = [&](unsigned at) {
            return static_cast<std::uint32_t>(input[at]) |
                   static_cast<std::uint32_t>(input[at + 1]) << 8 |
                   static_cast<std::uint32_t>(input[at + 2]) << 16 |
                   static_cast<std::uint32_t>(input[at + 3]) << 24;
        };
        const auto declared = u32(2), offset = u32(10), header = u32(14);
        if (declared > length || offset >= length || header < 40 || header > length - 14 ||
            offset < header + 14)
            return false;
        // stb can fill missing BMP rows with zeroes. Reject truncated raster data.
        const auto width = u32(18);
        const auto rawHeight = u32(22);
        const auto height = rawHeight & 0x80000000u ? 0u - rawHeight : rawHeight;
        const unsigned bits = input[28] | input[29] << 8;
        if (!width || width > 4096 || !height || height > 4096 ||
            (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
            (static_cast<std::uint64_t>(width) * bits + 31) / 32 * 4 * height > length - offset)
            return false;
    }
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(input, static_cast<int>(length), &width, &height, &channels))
        return false;
    if (bmp && height < 0 && height != INT_MIN)
        height = -height;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    stbi_uc* decoded =
        stbi_load_from_memory(input, static_cast<int>(length), &width, &height, &channels, 4);
    if (!decoded)
        return false;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    const std::size_t count = static_cast<std::size_t>(width) * height * 4;
    image.pixels.assign(decoded, decoded + count);
    stbi_image_free(decoded);
    return true;
}
bool DecodePngRgba(const void* bytes, std::size_t length, RgbaImage& image) {
    image = {};
    return bytes && length >= 8 && std::memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) == 0 &&
           DecodeTextureRgba(bytes, length, image);
}
std::uint64_t ImageRevision(const RgbaImage& image) {
    std::uint64_t revision = image.pixels.Fingerprint();
    revision = (revision ^ image.width) * 1099511628211ull;
    revision = (revision ^ image.height) * 1099511628211ull;
    return revision ? revision : 1;
}
bool NextPowerOfTwoTextureSize(std::uint32_t source, std::uint32_t limit, std::uint32_t& target) {
    target = 0;
    if (!source || !limit || source > 4096 || limit > 4096)
        return false;
    std::uint32_t rounded = 1;
    while (rounded < source)
        rounded *= 2;
    if (rounded > limit)
        return false;
    target = rounded;
    return true;
}
static bool PrepareLegacyTriangle(const RgbaImage& source, std::uint32_t width,
                                  std::uint32_t height, RgbaImage& prepared) {
    if (&source == &prepared) {
        const auto retained = source;
        return PrepareLegacyTriangle(retained, width, height, prepared);
    }
    prepared = {};
    if (!source.width || !source.height || source.width > 4096 || source.height > 4096 || !width ||
        !height || width > 4096 || height > 4096 ||
        source.pixels.size() != std::size_t(source.width) * source.height * 4)
        return false;
    if (source.width == width && source.height == height) {
        prepared = source;
        return true;
    }
    struct Tap {
        unsigned destination;
        float weight;
    };
    // D3DX9_43 x64's triangle filter builds destination coverage per source
    // texel, in float arithmetic. Rewriting this as a normalized gather or a
    // separable two-pass filter changes rounding before ordered dithering.
    const auto build = [](unsigned from, unsigned to) {
        std::vector<std::vector<Tap>> taps(from);
        const float ratio = float(to) / float(from), halfInverse = .5f / ratio;
        for (unsigned source = 0; source < from; ++source) {
            unsigned last = 0;
            float weight = 0;
            const auto flush = [&] {
                if (weight > 1.e-5f)
                    taps[source].push_back({last, weight});
            };
            for (unsigned half = 0; half < 2; ++half) {
                const float edge = (float(source) + float(half)) - .5f;
                const float low = edge * ratio, high = low + ratio;
                for (int d = int(std::floor(low)); float(d) < high; ++d) {
                    const auto wrapped = unsigned((d + int(to)) % int(to));
                    if (wrapped != last) {
                        flush();
                        weight = 0;
                        last = wrapped;
                    }
                    const float start = std::max(float(d), low);
                    const float end = std::min(float(d) + 1.f, high);
                    float slope = ((end + start) * halfInverse) - edge;
                    if (half)
                        slope = 1.f - slope;
                    const float area = (end - start) * slope;
                    weight = weight + area;
                }
            }
            flush();
        }
        return taps;
    };
    const auto columns = build(source.width, width), rows = build(source.height, height);
    std::vector<std::array<float, 4>> values(std::size_t(width) * height);
    constexpr float reciprocal = 1.f / 255.f;
    for (unsigned y = 0; y < source.height; ++y)
        for (unsigned x = 0; x < source.width; ++x) {
            const auto* raw = source.pixels.data() + (std::size_t(y) * source.width + x) * 4;
            const std::array<float, 4> pixel{raw[0] * reciprocal, raw[1] * reciprocal,
                                             raw[2] * reciprocal, raw[3] * reciprocal};
            for (const auto& row : rows[y])
                for (const auto& column : columns[x]) {
                    auto& value = values[std::size_t(row.destination) * width + column.destination];
                    const float weight = column.weight * row.weight;
                    for (unsigned c = 0; c < 4; ++c) {
                        const float contribution = pixel[c] * weight;
                        value[c] = value[c] + contribution;
                    }
                }
        }
    constexpr unsigned thresholds[4][4] = {
        {31, 15, 27, 11}, {19, 3, 23, 7}, {25, 9, 29, 13}, {21, 5, 17, 1}};
    prepared.width = width;
    prepared.height = height;
    prepared.pixels.resize(std::size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            for (unsigned c = 0; c < 4; ++c) {
                const float scaled = values[std::size_t(y) * width + x][c] * 255.f;
                const float value = std::floor(scaled + thresholds[y % 4][x % 4] / 32.f);
                prepared.pixels[(std::size_t(y) * width + x) * 4 + c] =
                    std::uint8_t(std::clamp(value, 0.f, 255.f));
            }
    prepared.pixels.Seal();
    return true;
}
bool PrepareTriangleDitherWrap(const RgbaImage& source, std::uint32_t width, std::uint32_t height,
                               RgbaImage& prepared) {
    if (width < source.width || height < source.height || width > source.width * 2 ||
        height > source.height * 2) {
        prepared = {};
        return false;
    }
    return PrepareLegacyTriangle(source, width, height, prepared);
}
bool PrepareBackgroundImage(const RgbaImage& source, RgbaImage& prepared) {
    return PrepareLegacyTriangle(source, 1024, 1024, prepared);
}

}
