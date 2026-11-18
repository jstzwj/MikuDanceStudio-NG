#include "mikudancestudio/wave_format.hpp"
#include "wave_layout.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace mikudancestudio::format {
namespace wave_detail {
namespace {
unsigned U16(const unsigned char* p) {
    return unsigned(p[0]) | (unsigned(p[1]) << 8);
}
std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(U16(p)) | (std::uint32_t(U16(p + 2)) << 16);
}
}
bool ReadLayout(const ReadAt& read, std::uint64_t size, Layout& output, std::string& reason) {
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    std::array<unsigned char, 40> header{};
    if (size < 12 || !read(0, header.data(), 12) || std::memcmp(header.data(), "RIFF", 4) ||
        std::memcmp(header.data() + 8, "WAVE", 4))
        return fail("invalid-wave-header");
    const std::uint64_t end = 8ull + U32(header.data() + 4);
    if (end > size || end < 12)
        return fail("truncated-wave");
    Layout next;
    unsigned chunkCount = 0;
    for (std::uint64_t at = 12; at < end;) {
        if (++chunkCount > 4096)
            return fail("wave-chunk-count-limit");
        if (end - at < 8 || !read(at, header.data(), 8))
            return fail("truncated-wave-chunk");
        const auto bytes = U32(header.data() + 4);
        const auto data = at + 8;
        if (bytes > end - data)
            return fail("truncated-wave-chunk");
        if (!std::memcmp(header.data(), "fmt ", 4)) {
            if (next.format || bytes < 16 ||
                !read(data, header.data(), std::min<unsigned>(40, bytes)))
                return fail("invalid-wave-format-chunk");
            next.format = U16(header.data());
            next.channels = U16(header.data() + 2);
            next.rate = U32(header.data() + 4);
            next.align = U16(header.data() + 12);
            next.bits = U16(header.data() + 14);
            if (next.format == 0xfffe) {
                const unsigned char tail[]{0, 0, 0,    0, 0x10, 0,    0x80,
                                           0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
                if (bytes < 40 || U16(header.data() + 16) < 22 ||
                    U16(header.data() + 18) != next.bits ||
                    std::memcmp(header.data() + 26, tail, 14))
                    return fail("unsupported-wave-extensible");
                next.format = U16(header.data() + 24);
            }
            if ((next.format != 1 && next.format != 3) || next.channels < 1 || next.channels > 2 ||
                next.rate < 8000 || next.rate > 192000 ||
                (next.format == 3
                     ? next.bits != 32
                     : next.bits != 8 && next.bits != 16 && next.bits != 24 && next.bits != 32) ||
                next.align != next.channels * (next.bits / 8) ||
                U32(header.data() + 8) != next.rate * next.align)
                return fail("unsupported-wave-format");
        } else if (!std::memcmp(header.data(), "data", 4))
            next.chunks.push_back({data, bytes, 0});
        at = data + bytes + (bytes & 1);
        if (at > end)
            return fail("missing-wave-padding");
    }
    if (!next.format || next.chunks.empty())
        return fail("wave-format-or-data-missing");
    for (auto& chunk : next.chunks) {
        if (chunk.bytes % next.align)
            return fail("wave-partial-sample-frame");
        chunk.firstFrame = next.frames;
        next.frames += chunk.bytes / next.align;
    }
    output = std::move(next);
    reason.clear();
    return true;
}
bool Decode(const unsigned char* bytes, std::size_t samples, const Layout& layout, float* output,
            std::string& reason) {
    for (std::size_t n = 0; n < samples; ++n, bytes += layout.bits / 8) {
        float sample;
        if (layout.format == 3) {
            const auto raw = U32(bytes);
            std::memcpy(&sample, &raw, 4);
            if (!std::isfinite(sample)) {
                reason = "nonfinite-wave-sample";
                return false;
            }
        } else if (layout.bits == 8)
            sample = (int(*bytes) - 128) / 128.f;
        else if (layout.bits == 16)
            sample = std::int16_t(U16(bytes)) / 32768.f;
        else if (layout.bits == 24) {
            int value = int(U16(bytes)) | (int(bytes[2]) << 16);
            if (value & 0x800000)
                value -= 0x1000000;
            sample = value / 8388608.f;
        } else
            sample = float(std::int32_t(U32(bytes)) / 2147483648.);
        output[n] = sample;
    }
    reason.clear();
    return true;
}
}
std::uint64_t WaveSource::FrameOffset(std::uint32_t frame) const {
    return std::min(SampleFrames(), std::uint64_t(frame) * sampleRate / 30);
}
bool WaveAudio::ReadFrames(std::uint64_t first, unsigned count, float* out,
                           std::string& reason) const {
    if (!channels || !out || count > MaxReadFrames) {
        reason = "wave-read-range-invalid";
        return false;
    }
    const auto available = first < SampleFrames()
                               ? unsigned(std::min<std::uint64_t>(count, SampleFrames() - first))
                               : 0;
    std::fill_n(out, std::size_t(count) * channels, 0.f);
    if (available)
        std::copy_n(samples.data() + first * channels, std::size_t(available) * channels, out);
    reason.clear();
    return true;
}
bool ReadWave(const std::vector<std::uint8_t>& bytes, WaveAudio& output, std::string& reason) {
    if (bytes.size() > (256u << 20)) {
        reason = "invalid-wave-header";
        return false;
    }
    wave_detail::Layout layout;
    if (!wave_detail::ReadLayout(
            [&](auto at, auto* out, auto count) {
                if (at > bytes.size() || count > bytes.size() - at)
                    return false;
                std::memcpy(out, bytes.data() + at, count);
                return true;
            },
            bytes.size(), layout, reason))
        return false;
    if (layout.frames * layout.channels > (128u << 20)) {
        reason = "wave-decoded-size-limit";
        return false;
    }
    WaveAudio next;
    next.sampleRate = layout.rate;
    next.channels = layout.channels;
    next.sourceBlockAlign = layout.align;
    next.samples.resize(std::size_t(layout.frames * layout.channels));
    for (const auto& chunk : layout.chunks)
        if (chunk.bytes &&
            !wave_detail::Decode(bytes.data() + chunk.offset,
                                 std::size_t(chunk.bytes / (layout.bits / 8)), layout,
                                 next.samples.data() + chunk.firstFrame * layout.channels, reason))
            return false;
    output = std::move(next);
    reason.clear();
    return true;
}
}
