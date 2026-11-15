#pragma once
#include "export.hpp"
#include "mikudancestudio/wave_format.hpp"
namespace mikudancestudio::qt {
enum class VideoProfile {
    Mp4,
    Matroska
};
// Qt's FFmpeg backend; no camera, audio device, screen capture or external exe.
// Ready-to-send backpressure bounds pending input, plus one caller-owned frame.
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();
    bool Open(const std::filesystem::path&, const render::ExportTimeline&, VideoProfile,
              const format::WaveAudio*, const std::function<bool()>& cancelCheck, std::string&);
    bool Write(const render::ExportFrame&, std::string&);
    bool Finish(std::string&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool ExportMovie(const render::ExportSnapshot&, const std::filesystem::path& builtins,
                 const rhi::BackendOptions&, const std::filesystem::path& destination,
                 const render::ExportSettings&, VideoProfile, bool audio, const ExportProgress&,
                 std::string&);
}
