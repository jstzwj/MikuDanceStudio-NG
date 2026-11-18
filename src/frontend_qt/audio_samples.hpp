#pragma once
#include "mikudancestudio/wave_format.hpp"
#include <QIODevice>
#include <QByteArray>
namespace mikudancestudio::qt {
// Value lease, no QObject or shared cursor. Native output threads own their
// read positions; WAV random reads are serialized inside its file lease.
struct AudioSamples {
    std::shared_ptr<const format::WaveSource> wave;
    QByteArray pcm; // compatibility for callers supplying an immutable QBuffer
    qint64 Size() const;
    bool Read(qint64 offset, char* out, qint64 bytes, std::string&) const;
};
class WaveDevice final : public QIODevice {
public:
    explicit WaveDevice(AudioSamples, qint64 origin = 0);
    qint64 size() const override;
    bool seek(qint64) override;
    const AudioSamples& Samples() const { return samples_; }
    const std::string& Failure() const { return failure_; }

protected:
    qint64 readData(char*, qint64) override;
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    AudioSamples samples_;
    qint64 origin_ = 0;
    std::string failure_;
};
bool CaptureAudioSamples(QIODevice&, AudioSamples&, std::string&);
}
