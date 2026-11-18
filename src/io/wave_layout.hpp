#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace mikudancestudio::format::wave_detail {
struct Chunk {
    std::uint64_t offset = 0, bytes = 0, firstFrame = 0;
};
struct Layout {
    unsigned format = 0, bits = 0, align = 0, channels = 0, rate = 0;
    std::uint64_t frames = 0;
    std::vector<Chunk> chunks;
};
using ReadAt = std::function<bool(std::uint64_t, unsigned char*, std::size_t)>;
bool ReadLayout(const ReadAt&, std::uint64_t size, Layout&, std::string&);
bool Decode(const unsigned char*, std::size_t samples, const Layout&, float*, std::string&);
}
