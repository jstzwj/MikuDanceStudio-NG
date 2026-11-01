#pragma once
#include "mikudancestudio/wave_format.hpp"
#include <QAudioSink>
#include <QBuffer>
#include <filesystem>
#include <memory>
namespace mikudancestudio::qt {
class AudioTransport {
public:
    bool Load(const std::filesystem::path&, std::string&);
    bool Play(double seconds, std::string&);
    void Stop();
    void Volume(float);
    double Position() const;
    bool Running() const;
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    format::WaveAudio wave_;
    QByteArray pcm_;
    QBuffer buffer_;
    std::unique_ptr<QAudioSink> sink_;
    double start_ = 0;
    float volume_ = 1;
};
}
