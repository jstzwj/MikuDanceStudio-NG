#include "audio_output.hpp"
#include <QAudioDevice>
#include <QBuffer>
#include <QMediaDevices>
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>

namespace mikudancestudio::qt {
namespace {
// AudioQueue owns its callback thread and presents a native queue timeline.
// Qt 6.8's Darwin processedUSecs counts render callbacks and ignores their
// AudioTimeStamp; it is not the presentation clock after a device discontinuity.
class CoreAudioOutput final : public AudioOutput {
public:
    ~CoreAudioOutput() override { Stop(); }
    bool Start(const QAudioFormat& format, QIODevice& source, float volume,
               std::string& reason) override {
        Stop();
        const auto* buffer = qobject_cast<QBuffer*>(&source);
        const auto device = QMediaDevices::defaultAudioOutput();
        if (!buffer || device.isNull() || !device.isFormatSupported(format) ||
            format.sampleFormat() != QAudioFormat::Float || format.sampleRate() <= 0 ||
            format.channelCount() < 1 || source.pos() < 0 || source.pos() >= buffer->size() ||
            source.pos() % format.bytesPerFrame() || buffer->size() % format.bytesPerFrame()) {
            reason = "audio-output-format-or-source-unavailable";
            return false;
        }
        pcm_ = buffer->data();
        start_ = source.pos();
        length_ = pcm_.size() - start_;
        format_ = format;
        duration_ = format.durationForBytes(length_);
        AudioStreamBasicDescription stream{};
        stream.mSampleRate = format.sampleRate();
        stream.mFormatID = kAudioFormatLinearPCM;
        stream.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
        stream.mBytesPerPacket = stream.mBytesPerFrame = UInt32(format.bytesPerFrame());
        stream.mFramesPerPacket = 1;
        stream.mChannelsPerFrame = UInt32(format.channelCount());
        stream.mBitsPerChannel = 32;
        stopping_ = false;
        auto status = AudioQueueNewOutput(&stream, &Refill, this, nullptr, nullptr, 0, &queue_);
        if (status == noErr)
            status = AudioQueueCreateTimeline(queue_, &timeline_);
        // Three frame-aligned buffers; the system consumes/refills them without
        // UI timers. No allocation or Qt object access occurs in the callback.
        const auto bytes = UInt32(format.bytesForFrames(std::max(1, format.sampleRate() / 20)));
        for (unsigned i = 0; status == noErr && i < 3 && nextByte_ < length_; ++i) {
            AudioQueueBufferRef buffer = nullptr;
            status = AudioQueueAllocateBuffer(queue_, bytes, &buffer);
            if (status == noErr && !Fill(queue_, buffer))
                status = status_.load();
        }
        if (status == noErr)
            status = AudioQueueSetParameter(queue_, kAudioQueueParam_Volume,
                                            std::clamp(volume, 0.f, 1.f));
        if (status == noErr)
            status = AudioQueueStart(queue_, nullptr);
        if (status != noErr) {
            reason = "audio-output-start-failed:" + std::to_string(status);
            Stop();
            return false;
        }
        reason.clear();
        return true;
    }
    void Stop() override {
        stopping_ = true;
        if (queue_) {
            // Immediate stop discards queued audio on pause/seek. Disposal
            // synchronizes callbacks before their immutable PCM owner is freed.
            AudioQueueStop(queue_, true);
            if (timeline_)
                AudioQueueDisposeTimeline(queue_, timeline_);
            AudioQueueDispose(queue_, true);
        }
        queue_ = nullptr;
        timeline_ = nullptr;
        pcm_.clear();
        nextByte_ = 0;
        pending_ = 0;
        status_ = noErr;
        draining_ = false;
        processed_ = duration_ = start_ = length_ = 0;
        discontinuities_ = 0;
    }
    void Volume(float value) override {
        if (queue_)
            Store(AudioQueueSetParameter(queue_, kAudioQueueParam_Volume,
                                         std::clamp(value, 0.f, 1.f)));
    }
    bool Running() const override {
        if (!queue_ || stopping_ || status_ != noErr)
            return false;
        // A buffer callback only means its data was acquired, not played. Only
        // the native asynchronous drain completion ends this clock.
        return !draining_ || NativeRunning();
    }
    qint64 ProcessedMicroseconds() const override {
        if (!queue_)
            return 0;
        AudioTimeStamp stamp{};
        Boolean discontinuity = false;
        const auto status = AudioQueueGetCurrentTime(queue_, timeline_, &stamp, &discontinuity);
        // The queue may not have a timestamp before its first render, or after
        // its asynchronous drain. Other failures invalidate the device clock.
        if (status != kAudioQueueErr_InvalidRunState)
            Store(status);
        if (status == noErr && (stamp.mFlags & kAudioTimeStampSampleTimeValid) &&
            std::isfinite(stamp.mSampleTime)) {
            const auto seconds =
                std::clamp(stamp.mSampleTime / format_.sampleRate(), 0., duration_ / 1e6);
            processed_ = std::max(processed_, qint64(seconds * 1e6));
            if (discontinuity)
                ++discontinuities_;
        }
        if (draining_ && !NativeRunning() && status_ == noErr)
            processed_ = duration_;
        return processed_;
    }
    std::string Error() const override {
        const auto status = status_.load();
        return status == noErr ? std::string{}
                               : "audio-output-device-failed:" + std::to_string(status);
    }
    std::string Diagnostic() const override {
        std::ostringstream out;
        out << "audioqueue running=" << NativeRunning() << " status=" << status_.load()
            << " queued-bytes=" << std::min(nextByte_.load(), length_) << '/' << length_
            << " pending=" << pending_.load() << " draining=" << draining_.load()
            << " processed-us=" << ProcessedMicroseconds() << " duration-us=" << duration_
            << " discontinuities=" << discontinuities_;
        return out.str();
    }

private:
    void Store(OSStatus status) const {
        if (status != noErr)
            status_ = status;
    }
    bool NativeRunning() const {
        if (!queue_)
            return false;
        UInt32 running = 0, bytes = sizeof running;
        const auto status =
            AudioQueueGetProperty(queue_, kAudioQueueProperty_IsRunning, &running, &bytes);
        Store(status);
        return status == noErr && running != 0;
    }
    bool Fill(AudioQueueRef queue, AudioQueueBufferRef buffer) {
        if (stopping_)
            return false;
        const auto offset = nextByte_.fetch_add(buffer->mAudioDataBytesCapacity);
        if (offset >= length_)
            return false;
        const auto bytes =
            UInt32(std::min<qint64>(buffer->mAudioDataBytesCapacity, length_ - offset));
        std::memcpy(buffer->mAudioData, pcm_.constData() + start_ + offset, bytes);
        buffer->mAudioDataByteSize = bytes;
        ++pending_;
        const auto status = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
        if (status != noErr)
            --pending_;
        Store(status);
        return status == noErr;
    }
    static void Refill(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer) {
        auto& self = *static_cast<CoreAudioOutput*>(context);
        --self.pending_;
        if (self.stopping_ || self.status_ != noErr)
            return;
        if (!self.Fill(queue, buffer) && self.pending_ == 0 && self.status_ == noErr) {
            self.draining_ = true;
            self.Store(AudioQueueStop(queue, false));
        }
    }
    AudioQueueRef queue_ = nullptr;
    AudioQueueTimelineRef timeline_ = nullptr;
    QAudioFormat format_;
    QByteArray pcm_;
    qint64 start_ = 0, length_ = 0, duration_ = 0;
    std::atomic<qint64> nextByte_{0};
    std::atomic<unsigned> pending_{0};
    std::atomic<bool> stopping_{true}, draining_{false};
    mutable std::atomic<OSStatus> status_{noErr};
    mutable qint64 processed_ = 0;
    mutable unsigned discontinuities_ = 0;
};
}
std::unique_ptr<AudioOutput> CreateCoreAudioOutput() {
    return std::make_unique<CoreAudioOutput>();
}
}
