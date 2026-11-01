#pragma once

#include <cstddef>
#include <cstdint>

namespace mikudancestudio {

struct TexturedScreenVertex {
    float x, y, z, reciprocalW;
    std::uint32_t color;
    float u, v;
};

struct ColoredScreenVertex {
    float x, y, z, reciprocalW;
    std::uint32_t color;
};

// CPU transport descriptors use the original numeric FVF values without
// importing DirectX into portable geometry consumers.
inline constexpr std::uint32_t kTexturedScreenVertexFvf = 0x144;
inline constexpr std::uint32_t kColoredScreenVertexFvf = 0x44;
inline constexpr std::size_t kTextOverlayVertexCapacity = 1000;
inline constexpr std::size_t kSpriteOverlayVertexCapacity = 10000;
inline constexpr std::size_t kLineOverlayVertexCapacity = 10000;
inline constexpr std::size_t kBackgroundQuadVertexCount = 6;
inline constexpr std::size_t kBackgroundQuadBytes =
    kBackgroundQuadVertexCount * sizeof(TexturedScreenVertex);

static_assert(sizeof(TexturedScreenVertex) == 28);
static_assert(offsetof(TexturedScreenVertex, reciprocalW) == 12);
static_assert(offsetof(TexturedScreenVertex, color) == 16);
static_assert(offsetof(TexturedScreenVertex, u) == 20);
static_assert(offsetof(TexturedScreenVertex, v) == 24);
static_assert(sizeof(ColoredScreenVertex) == 20);
static_assert(offsetof(ColoredScreenVertex, color) == 16);

} // namespace mikudancestudio
