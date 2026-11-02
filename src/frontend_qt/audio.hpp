#pragma once
#include "mikudancestudio/wave_format.hpp"
#include "audio_output.hpp"
#include <QBuffer>
#include <filesystem>
#include <memory>
#include <optional>
namespace mikudancestudio::qt {
class AudioTransport {
public:
    explicit AudioTransport(std::unique_ptr<AudioOutput> = {}, bool disableNativeOutput = false);
    bool Load(const std::filesystem::path&, std::string&);
    bool Play(double seconds, std::string&);
    void Stop();
    void Volume(float);
    double Position() const;
    // Reports the final processed position once before handing the clock back
    // to the editor timer when the WAV reaches its end.
    std::optional<double> ClockPosition();
    void ReleaseClock() { clockActive_ = false; }
    bool Running() const;
    std::string Error() const;
    std::string Diagnostic() const;
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    format::WaveAudio wave_;
    QByteArray pcm_;
    QBuffer buffer_;
    std::unique_ptr<AudioOutput> output_;
    double start_ = 0;
    float volume_ = 1;
    bool clockActive_ = false;
};
}
