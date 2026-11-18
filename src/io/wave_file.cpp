#include "mikudancestudio/wave_format.hpp"
#include "wave_layout.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>
namespace mikudancestudio::format {
namespace {
class FileWave final : public WaveSource {
public:
    mutable std::ifstream file;
    wave_detail::Layout layout;
    mutable std::mutex mutex;
    std::uint64_t SampleFrames() const override { return layout.frames; }
    bool Bytes(std::uint64_t at, unsigned char* output, std::size_t bytes) const {
        file.clear();
        file.seekg(std::streamoff(at));
        return bool(file.read(reinterpret_cast<char*>(output), std::streamsize(bytes)));
    }
    bool ReadFrames(std::uint64_t first, unsigned count, float* output,
                    std::string& reason) const override {
        if (!output || count > MaxReadFrames) {
            reason = "wave-read-range-invalid";
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        std::fill_n(output, std::size_t(count) * channels, 0.f);
        std::array<unsigned char, MaxReadFrames * 8> bytes;
        const auto end = first + std::min<std::uint64_t>(count, UINT64_MAX - first);
        for (const auto& chunk : layout.chunks) {
            const auto start = std::max(first, chunk.firstFrame);
            const auto stop = std::min(end, chunk.firstFrame + chunk.bytes / layout.align);
            if (start >= stop)
                continue;
            const auto readFrames = unsigned(stop - start);
            if (!Bytes(chunk.offset + (start - chunk.firstFrame) * layout.align, bytes.data(),
                       std::size_t(readFrames) * layout.align)) {
                reason = "wave-file-read-failed";
                return false;
            }
            if (!wave_detail::Decode(bytes.data(), std::size_t(readFrames) * channels, layout,
                                     output + (start - first) * channels, reason))
                return false;
        }
        reason.clear();
        return true;
    }
};
}
bool OpenWaveFile(const std::filesystem::path& path, std::shared_ptr<const WaveSource>& output,
                  std::string& reason) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        reason = "wave-file-read-failed";
        return false;
    }
    auto next = std::make_shared<FileWave>();
    next->file.open(path, std::ios::binary | std::ios::ate);
    const auto size = next->file ? next->file.tellg() : std::streampos(-1);
    if (size < 0) {
        reason = "wave-file-read-failed";
        return false;
    }
    if (!wave_detail::ReadLayout(
            [&](auto at, auto* bytes, auto count) { return next->Bytes(at, bytes, count); },
            std::uint64_t(size), next->layout, reason))
        return false;
    const auto& layout = next->layout;
    if (layout.frames > std::uint64_t(layout.rate) * 21600) {
        reason = "wave-duration-limit";
        return false;
    }
    next->sampleRate = layout.rate;
    next->channels = layout.channels;
    next->sourceBlockAlign = layout.align;
    output = std::move(next);
    reason.clear();
    return true;
}
}
