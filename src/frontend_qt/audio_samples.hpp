#pragma once
#include "mikudancestudio/wave_format.hpp"
#include <QIODevice>
#include <QByteArray>
#include <QAudioFormat>
namespace mikudancestudio::qt {
// QAudioFormat's byte conversion accepts qint32, even on 64-bit platforms.
// Native queues need the full streamed source duration, not a narrowed count.
qint64 AudioDurationMicroseconds(const QAudioFormat&, qint64 bytes);
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
    explicit WaveDevice(AudioSamples, qint64 origin = 0, bool int16Output = false);
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
    bool int16Output_ = false;
    qint64 ReadInt16(char*, qint64, std::string&);
    std::string failure_;
};
bool CaptureAudioSamples(QIODevice&, AudioSamples&, std::string&);
}
