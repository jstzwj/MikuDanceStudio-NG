#pragma once
#include <QAudioFormat>
#include <QIODevice>
#include <memory>
#include <string>
namespace mikudancestudio::qt {
// The transport owns decoded PCM and positioning. Only this adapter touches
// the platform audio device; ordinary tests use a deterministic consuming sink.
class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual bool Start(const QAudioFormat&, QIODevice&, float volume, std::string&) = 0;
    virtual void Stop() = 0;
    virtual void Volume(float) = 0;
    virtual bool Running() const = 0;
    virtual qint64 ProcessedMicroseconds() const = 0;
    virtual std::string Error() const { return {}; }
};
std::unique_ptr<AudioOutput> CreateAudioOutput();
}
