#include "mikudancestudio/wave_format.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mikudancestudio::format {
std::uint64_t WaveAudio::FrameOffset(std::uint32_t frame) const {
    return std::min(SampleFrames(), std::uint64_t(frame) * sampleRate / 30);
}
bool ReadWave(const std::vector<std::uint8_t>& bytes, WaveAudio& output, std::string& reason) {
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    if (bytes.size() < 12 || bytes.size() > (256u << 20) || std::memcmp(bytes.data(), "RIFF", 4) ||
        std::memcmp(bytes.data() + 8, "WAVE", 4))
        return fail("invalid-wave-header");
    const auto u16 = [&](std::size_t at) {
        return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8);
    };
    const auto u32 = [&](std::size_t at) {
        return std::uint32_t(u16(at)) | (std::uint32_t(u16(at + 2)) << 16);
    };
    const std::uint64_t end = 8ull + u32(4);
    if (end > bytes.size() || end < 12)
        return fail("truncated-wave");
    unsigned format = 0, bits = 0, align = 0;
    WaveAudio next;
    std::vector<std::pair<std::size_t, std::size_t>> chunks;
    for (std::size_t at = 12; at < end;) {
        if (end - at < 8)
            return fail("truncated-wave-chunk");
        const auto size = u32(at + 4);
        const auto data = at + 8;
        if (size > end - data)
            return fail("truncated-wave-chunk");
        if (!std::memcmp(bytes.data() + at, "fmt ", 4)) {
            if (format || size < 16)
                return fail("invalid-wave-format-chunk");
            format = u16(data);
            next.channels = u16(data + 2);
            next.sampleRate = u32(data + 4);
            align = u16(data + 12);
            bits = u16(data + 14);
            if (format == 0xfffe) {
                const std::uint8_t guidTail[]{0, 0, 0,    0, 0x10, 0,    0x80,
                                              0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
                if (size < 40 || u16(data + 16) < 22 || u16(data + 18) != bits ||
                    std::memcmp(bytes.data() + data + 26, guidTail, 14))
                    return fail("unsupported-wave-extensible");
                format = u16(data + 24);
            }
            if ((format != 1 && format != 3) || next.channels < 1 || next.channels > 2 ||
                next.sampleRate < 8000 || next.sampleRate > 192000 ||
                (format == 3 ? bits != 32 : bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
                align != next.channels * (bits / 8) || u32(data + 8) != next.sampleRate * align)
                return fail("unsupported-wave-format");
        } else if (!std::memcmp(bytes.data() + at, "data", 4))
            chunks.push_back({data, size});
        at = data + size + (size & 1);
        if (at > end)
            return fail("missing-wave-padding");
    }
    if (!format || chunks.empty())
        return fail("wave-format-or-data-missing");
    for (const auto& chunk : chunks) {
        if (chunk.second % align)
            return fail("wave-partial-sample-frame");
        if (next.samples.size() + chunk.second / (bits / 8) > (128u << 20))
            return fail("wave-decoded-size-limit");
        for (std::size_t at = chunk.first; at < chunk.first + chunk.second; at += bits / 8) {
            float sample = 0;
            if (format == 3) {
                const auto raw = u32(at);
                std::memcpy(&sample, &raw, 4);
                if (!std::isfinite(sample))
                    return fail("nonfinite-wave-sample");
            } else if (bits == 8)
                sample = (int(bytes[at]) - 128) / 128.f;
            else if (bits == 16)
                sample = std::int16_t(u16(at)) / 32768.f;
            else if (bits == 24) {
                int n = int(u16(at)) | (int(bytes[at + 2]) << 16);
                if (n & 0x800000)
                    n -= 0x1000000;
                sample = n / 8388608.f;
            } else
                sample = float(std::int32_t(u32(at)) / 2147483648.);
            next.samples.push_back(sample);
        }
    }
    output = std::move(next);
    reason.clear();
    return true;
}
}
