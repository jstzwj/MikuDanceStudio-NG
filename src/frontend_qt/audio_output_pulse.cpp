#include "audio_output.hpp"
#include <QAudioDevice>
#include <QBuffer>
#include <QMediaDevices>
#include <pulse/pulseaudio.h>
#include <pulse/timeval.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace mikudancestudio::qt {
namespace {
// The server requests PCM on its own thread. Neither refill nor the played
// sample clock depends on the editor's event loop or Qt's pull-mode timer.
class PulseAudioOutput final : public AudioOutput {
public:
    ~PulseAudioOutput() override { Stop(); }
    bool Start(const QAudioFormat& format, QIODevice& source, float volume,
               std::string& reason) override {
        Stop();
        const auto* buffer = qobject_cast<QBuffer*>(&source);
        const auto device = QMediaDevices::defaultAudioOutput();
        if (!buffer || device.isNull() || !device.isFormatSupported(format) ||
            format.sampleFormat() != QAudioFormat::Float || format.channelCount() < 1 ||
            format.channelCount() > PA_CHANNELS_MAX || format.sampleRate() <= 0 ||
            source.pos() < 0 || source.pos() >= buffer->size()) {
            reason = "audio-output-format-or-source-unavailable";
            return false;
        }
        pcm_ = buffer->data();
        cursor_ = source.pos();
        start_ = cursor_;
        format_ = format;
        if (cursor_ % format.bytesPerFrame() || pcm_.size() % format.bytesPerFrame()) {
            reason = "audio-output-source-unavailable";
            Stop();
            return false;
        }
        duration_ = format.durationForBytes(pcm_.size() - start_);
        volume_ = std::clamp(volume, 0.f, 1.f);
        loop_ = pa_threaded_mainloop_new();
        if (loop_)
            context_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "MikuDanceStudio-NG");
        if (!context_ || pa_threaded_mainloop_start(loop_) < 0) {
            reason = "audio-output-start-failed";
            Stop();
            return false;
        }
        loopStarted_ = true;
        bool ready = false;
        {
            Lock locked(loop_);
            timeval deadline;
            pa_gettimeofday(&deadline);
            pa_timeval_add(&deadline, 2000000);
            auto* api = pa_threaded_mainloop_get_api(loop_);
            deadline_ = api->time_new(api, &deadline, &Timeout, this);
            if (!deadline_)
                error_ = "audio-output-start-failed";
            pa_context_set_state_callback(context_, &ContextState, this);
            if (error_.empty() &&
                pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
                Fail();
            Wait([&] { return pa_context_get_state(context_) == PA_CONTEXT_READY; });
            if (error_.empty()) {
                pa_sample_spec spec{PA_SAMPLE_FLOAT32NE, std::uint32_t(format.sampleRate()),
                                    std::uint8_t(format.channelCount())};
                pa_channel_map map;
                if (pa_channel_map_init_auto(&map, spec.channels, PA_CHANNEL_MAP_DEFAULT))
                    stream_ = pa_stream_new(context_, "Editor WAV", &spec, &map);
                if (!stream_)
                    Fail();
            }
            if (stream_ && error_.empty()) {
                pa_stream_set_state_callback(stream_, &StreamState, this);
                pa_stream_set_write_callback(stream_, &WriteRequested, this);
                const auto target = std::uint32_t(format.bytesForDuration(100000));
                pa_buffer_attr attributes{target * 2, target, 0,
                                          std::uint32_t(format.bytesForDuration(5000)),
                                          std::uint32_t(-1)};
                // The interpolator can extrapolate before the cold sink has
                // supplied its first latency measurement, then freeze for the
                // entire correction. Poll the server's actual sample position
                // on the native mainloop instead of publishing that estimate.
                const auto flags =
                    pa_stream_flags_t(PA_STREAM_START_CORKED | PA_STREAM_AUTO_TIMING_UPDATE |
                                      PA_STREAM_ADJUST_LATENCY);
                // prebuf=0 prevents a finite tail from waiting for samples that
                // do not exist after an underrun. Drain is still real playback;
                // no padding samples or elapsed-time replacement clock is used.
                if (pa_stream_connect_playback(stream_, nullptr, &attributes, flags, nullptr,
                                               nullptr) < 0)
                    Fail();
                Wait([&] { return pa_stream_get_state(stream_) == PA_STREAM_READY; });
                if (error_.empty()) {
                    timeval now;
                    pa_gettimeofday(&now);
                    timing_ = api->time_new(api, &now, &RefreshTiming, this);
                    if (!timing_)
                        Fail();
                    Write(pa_stream_writable_size(stream_));
                    operationDone_ = false;
                    auto* operation = pa_stream_cork(stream_, 0, &OperationDone, this);
                    if (!operation)
                        Fail();
                    else {
                        Wait([&] { return operationDone_; });
                        pa_operation_unref(operation);
                    }
                }
            }
            if (deadline_) {
                api->time_free(deadline_);
                deadline_ = nullptr;
            }
            ready = error_.empty() && stream_ && operationDone_;
            reason = error_;
        }
        if (!ready)
            Stop();
        return ready;
    }
    void Stop() override {
        if (loop_) {
            if (loopStarted_)
                pa_threaded_mainloop_lock(loop_);
            if (deadline_)
                pa_threaded_mainloop_get_api(loop_)->time_free(deadline_);
            deadline_ = nullptr;
            if (timing_)
                pa_threaded_mainloop_get_api(loop_)->time_free(timing_);
            timing_ = nullptr;
            if (drain_) {
                pa_operation_cancel(drain_);
                pa_operation_unref(drain_);
                drain_ = nullptr;
            }
            if (stream_) {
                pa_stream_set_state_callback(stream_, nullptr, nullptr);
                pa_stream_set_write_callback(stream_, nullptr, nullptr);
                pa_stream_disconnect(stream_);
                pa_stream_unref(stream_);
                stream_ = nullptr;
            }
            if (context_) {
                pa_context_set_state_callback(context_, nullptr, nullptr);
                pa_context_disconnect(context_);
                pa_context_unref(context_);
                context_ = nullptr;
            }
            if (loopStarted_) {
                pa_threaded_mainloop_unlock(loop_);
                // Disconnect discards queued PCM; never drain on pause/seek.
                pa_threaded_mainloop_stop(loop_);
            }
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
        }
        loopStarted_ = false;
        pcm_.clear();
        processed_ = duration_ = cursor_ = start_ = 0;
        error_.clear();
        nativeError_.clear();
        drained_ = operationDone_ = false;
    }
    void Volume(float volume) override {
        if (loop_) {
            Lock locked(loop_);
            volume_ = std::clamp(volume, 0.f, 1.f);
        }
    }
    bool Running() const override {
        if (!loop_)
            return false;
        Lock locked(loop_);
        return error_.empty() && stream_ && (!drained_ || ProcessedLocked() < duration_);
    }
    qint64 ProcessedMicroseconds() const override {
        if (!loop_)
            return 0;
        Lock locked(loop_);
        return ProcessedLocked();
    }
    std::string Error() const override {
        if (!loop_)
            return {};
        Lock locked(loop_);
        return error_;
    }
    std::string Diagnostic() const override {
        if (!loop_)
            return "no-pulse-stream";
        Lock locked(loop_);
        std::ostringstream result;
        result << "pulse state=" << (stream_ ? int(pa_stream_get_state(stream_)) : -1)
               << " source=" << cursor_ - start_ << '/' << pcm_.size() - start_
               << " processed-us=" << ProcessedLocked() << " duration-us=" << duration_
               << " drained=" << drained_ << " error=" << nativeError_;
        if (stream_)
            if (const auto* timing = pa_stream_get_timing_info(stream_))
                result << " read=" << timing->read_index << " write=" << timing->write_index
                       << " sink-us=" << timing->sink_usec << " playing=" << timing->playing
                       << " configured-sink-us=" << timing->configured_sink_usec;
        return result.str();
    }

private:
    struct Lock {
        explicit Lock(pa_threaded_mainloop* loop) : loop(loop) { pa_threaded_mainloop_lock(loop); }
        ~Lock() { pa_threaded_mainloop_unlock(loop); }
        pa_threaded_mainloop* loop;
    };
    template <class F> void Wait(F ready) {
        while (error_.empty() && !ready())
            pa_threaded_mainloop_wait(loop_);
    }
    void Fail() {
        error_ = "audio-output-device-failed";
        nativeError_ = context_ ? pa_strerror(pa_context_errno(context_)) : "no-context";
        pa_threaded_mainloop_signal(loop_, 0);
    }
    qint64 ProcessedLocked() const {
        pa_usec_t played;
        if (stream_ && pa_stream_get_time(stream_, &played) >= 0)
            processed_ = std::max(processed_, std::min(duration_, qint64(played)));
        return processed_;
    }
    void Write(std::size_t requested) {
        if (!error_.empty() || requested == std::size_t(-1)) {
            if (error_.empty())
                Fail();
            return;
        }
        std::array<float, 8192> scaled;
        while (requested && cursor_ < pcm_.size()) {
            auto bytes = std::min({requested, std::size_t(pcm_.size() - cursor_), sizeof scaled});
            bytes -= bytes % format_.bytesPerFrame();
            if (!bytes)
                break;
            const void* data = pcm_.constData() + cursor_;
            if (volume_ != 1) {
                for (std::size_t i = 0; i < bytes / sizeof(float); ++i) {
                    float value;
                    std::memcpy(&value, pcm_.constData() + cursor_ + i * sizeof(float),
                                sizeof value);
                    scaled[i] = value * volume_;
                }
                data = scaled.data();
            }
            if (pa_stream_write(stream_, data, bytes, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
                Fail();
                return;
            }
            cursor_ += qint64(bytes);
            requested -= bytes;
        }
        if (cursor_ == pcm_.size() && !drain_) {
            drain_ = pa_stream_drain(stream_, &Drained, this);
            if (!drain_)
                Fail();
        }
    }
    static void ContextState(pa_context* context, void* data) {
        auto& self = *static_cast<PulseAudioOutput*>(data);
        if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(context)))
            self.Fail();
        pa_threaded_mainloop_signal(self.loop_, 0);
    }
    static void StreamState(pa_stream* stream, void* data) {
        auto& self = *static_cast<PulseAudioOutput*>(data);
        if (!PA_STREAM_IS_GOOD(pa_stream_get_state(stream)))
            self.Fail();
        pa_threaded_mainloop_signal(self.loop_, 0);
    }
    static void WriteRequested(pa_stream*, std::size_t bytes, void* data) {
        static_cast<PulseAudioOutput*>(data)->Write(bytes);
    }
    static void OperationDone(pa_stream*, int success, void* data) {
        auto& self = *static_cast<PulseAudioOutput*>(data);
        self.operationDone_ = true;
        if (!success)
            self.Fail();
        pa_threaded_mainloop_signal(self.loop_, 0);
    }
    static void Drained(pa_stream*, int success, void* data) {
        auto& self = *static_cast<PulseAudioOutput*>(data);
        self.drained_ = success != 0;
        if (!success)
            self.Fail();
        pa_threaded_mainloop_signal(self.loop_, 0);
    }
    static void Timeout(pa_mainloop_api*, pa_time_event*, const timeval*, void* data) {
        auto& self = *static_cast<PulseAudioOutput*>(data);
        self.error_ = "audio-output-start-timeout";
        pa_threaded_mainloop_signal(self.loop_, 0);
    }
    static void RefreshTiming(pa_mainloop_api* api, pa_time_event* event, const timeval*,
                              void* data) {
        auto& self = *static_cast<PulseAudioOutput*>(data);
        if (self.stream_ && pa_stream_get_state(self.stream_) == PA_STREAM_READY) {
            auto* operation = pa_stream_update_timing_info(self.stream_, nullptr, nullptr);
            if (operation)
                pa_operation_unref(operation);
            else
                self.Fail();
        }
        timeval next;
        pa_gettimeofday(&next);
        pa_timeval_add(&next, 10000);
        api->time_restart(event, &next);
    }
    pa_threaded_mainloop* loop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
    pa_operation* drain_ = nullptr;
    pa_time_event* deadline_ = nullptr;
    pa_time_event* timing_ = nullptr;
    bool loopStarted_ = false, operationDone_ = false, drained_ = false;
    QByteArray pcm_;
    QAudioFormat format_;
    qint64 start_ = 0, cursor_ = 0, duration_ = 0;
    mutable qint64 processed_ = 0;
    float volume_ = 1;
    std::string error_, nativeError_;
};
}
std::unique_ptr<AudioOutput> CreatePulseAudioOutput() {
    return std::make_unique<PulseAudioOutput>();
}
}
