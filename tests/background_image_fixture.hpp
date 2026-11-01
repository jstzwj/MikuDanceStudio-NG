#pragma once
#include "mikudancestudio/rhi_image.hpp"
#include <array>

struct BackgroundImageCase {
    unsigned width, height, pattern;
    std::uint64_t nativeHash;
};
// Expected digests are recorded by background_filter_oracle_test from
// Microsoft's x64 D3DX9_43 output, never from the portable preparation.
inline constexpr BackgroundImageCase kBackgroundImages[] = {
    {1, 1, 0, 0x6d16bee561c22325ull},       {1, 43, 0, 0xed452ed9e8f77794ull},
    {79, 1, 0, 0xa015fcaba3b95cb2ull},      {3, 3, 0, 0x0cd955cf46dabf13ull},
    {79, 43, 0, 0xde0dca2419627e28ull},     {511, 129, 0, 0xd96888f198a0954cull},
    {1024, 33, 0, 0x7a17bb9f7a317481ull},   {79, 1024, 0, 0x53d11abd57d3ed66ull},
    {1024, 1024, 0, 0xbe868fe1474444ebull}, {2048, 1536, 0, 0xa5452158102b73a7ull},
    {4096, 2, 0, 0x1358165c13e6b667ull},    {1, 1, 1, 0xc13dbeeea1222325ull},
    {1, 43, 1, 0x4ea1257408378325ull},      {79, 1, 1, 0x4de56bafb76312e2ull},
    {3, 3, 1, 0xed98ae8160b39372ull},       {79, 43, 1, 0xab6d40cc905fb267ull},
    {511, 129, 1, 0xdbaeee51156404c1ull},   {1024, 33, 1, 0xe4845e4cfe7ecb5bull},
    {79, 1024, 1, 0xb7592d65029570a0ull},   {1024, 1024, 1, 0x4d6d1805a8821d29ull},
    {2048, 1536, 1, 0xf6b0e586b450db62ull}, {4096, 2, 1, 0xbae18daca9181958ull}};
inline mikudancestudio::rhi::RgbaImage BackgroundImage(const BackgroundImageCase& c) {
    mikudancestudio::rhi::RgbaImage image;
    image.width = c.width;
    image.height = c.height;
    image.pixels.resize(std::size_t(c.width) * c.height * 4);
    std::uint32_t random = 0x789abcd;
    for (unsigned y = 0; y < c.height; ++y)
        for (unsigned x = 0; x < c.width; ++x) {
            auto* p = image.pixels.data() + (std::size_t(y) * c.width + x) * 4;
            if (c.pattern) {
                for (unsigned i = 0; i < 4; ++i) {
                    random = random * 1664525u + 1013904223u;
                    p[i] = std::uint8_t(random >> 24);
                }
            } else {
                p[0] = c.width == 1 ? 193 : std::uint8_t(x * 255 / (c.width - 1));
                p[1] = c.height == 1 ? 29 : std::uint8_t(y * 255 / (c.height - 1));
                p[2] = (x + y) % 2 ? 224 : 32;
                p[3] = (x + y) % 5 == 0 ? 0 : (x + y) % 5 == 1 ? 1 : x < c.width / 2 ? 96 : 255;
            }
        }
    image.pixels.Seal();
    return image;
}
inline std::vector<std::uint8_t> BackgroundTga(const mikudancestudio::rhi::RgbaImage& image) {
    std::vector<std::uint8_t> bytes(18 + image.pixels.size());
    bytes[2] = 2;
    bytes[12] = std::uint8_t(image.width);
    bytes[13] = std::uint8_t(image.width >> 8);
    bytes[14] = std::uint8_t(image.height);
    bytes[15] = std::uint8_t(image.height >> 8);
    bytes[16] = 32;
    bytes[17] = 0x28;
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        bytes[18 + i] = image.pixels[i + 2];
        bytes[19 + i] = image.pixels[i + 1];
        bytes[20 + i] = image.pixels[i];
        bytes[21 + i] = image.pixels[i + 3];
    }
    return bytes;
}
inline std::uint64_t BackgroundImageHash(const mikudancestudio::rhi::RgbaImage& image) {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : image.pixels)
        hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}
