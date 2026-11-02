#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QFile>
#include <QDataStream>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " " << reason << '\n';               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
class ConsumingOutput final : public qt::AudioOutput {
public:
    bool Start(const QAudioFormat& f, QIODevice& data, float, std::string& error) override {
        ++starts;
        if (fail) {
            error = "injected-output-failure";
            return false;
        }
        format = f;
        source = &data;
        offset = data.pos();
        elapsed = 0;
        running = true;
        deviceFailed = false;
        error.clear();
        return true;
    }
    void Stop() override {
        running = false;
        source = nullptr;
        elapsed = 0;
    }
    void Volume(float) override {}
    bool Running() const override { return running; }
    qint64 ProcessedMicroseconds() const override { return elapsed; }
    std::string Error() const override {
        return deviceFailed ? "injected-streaming-device-failure" : "";
    }
    void Advance(qint64 microseconds) {
        if (!running || !source)
            return;
        const auto before = format.bytesForDuration(elapsed);
        elapsed += microseconds;
        source->read(format.bytesForDuration(elapsed) - before);
        if (source->atEnd())
            running = false;
    }
    QAudioFormat format;
    QIODevice* source = nullptr;
    qint64 offset = 0, elapsed = 0;
    unsigned starts = 0;
    bool running = false, fail = false, deviceFailed = false;
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 2);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    QFile wave(dir.path() + "/clock.wav");
    CHECK(wave.open(QIODevice::WriteOnly));
    constexpr quint32 frames = 8000 * 180;
    QDataStream out(&wave);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + frames);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(8000) << quint16(1)
        << quint16(8);
    out.writeRawData("data", 4);
    out << frames;
    QByteArray pcm(frames, '\0');
    for (int i = 0; i < pcm.size(); ++i)
        pcm[i] = char(i % 256);
    CHECK(out.writeRawData(pcm.data(), pcm.size()) == pcm.size());
    wave.close();
    auto output = std::make_unique<ConsumingOutput>();
    auto* sink = output.get();
    qt::EditorWindow window(std::filesystem::absolute(argv[1]), true, std::move(output));
    auto& session = window.Session();
    CHECK(session.ImportWave(qt::Path(wave.fileName()), reason));
    CHECK(session.Edit(
        "playback-range",
        [](auto& d, auto&) {
            d.timeline.playStartFrame = 30;
            d.timeline.playStopFrame = 4500;
            d.timeline.startAtCurrent = 0;
            d.timeline.playbackLoop = 0;
            d.timeline.returnToStart = 0;
            return true;
        },
        reason));
    window.Dispatch(panel::kPlayButton);
    CHECK(session.Playing() && sink->Running());
    CHECK(sink->offset == 8000 * 4 && sink->format.sampleFormat() == QAudioFormat::Float);
    for (int i = 1; i <= 600; ++i) {
        sink->Advance(200000);
        window.Tick(i % 2 ? .001 : .9); // UI timer jitter never becomes the audio clock.
        CHECK(std::abs(session.CursorSeconds() - (1 + i * .2)) < 1e-9);
    }
    const auto updates = session.Runtime().Counters().frames;
    window.Tick(.3); // no newly processed audio: do not evaluate a duplicate frame.
    CHECK(session.Runtime().Counters().frames == updates);
    window.Dispatch(panel::kPlayButton);
    CHECK(!session.Playing() && !sink->Running());
    CHECK(session.Edit(
        "loop-range",
        [](auto& d, auto&) {
            d.timeline.playStopFrame = 33;
            d.timeline.playbackLoop = 1;
            return true;
        },
        reason));
    window.Dispatch(panel::kPlayButton);
    const auto epoch = session.PlaybackEpoch();
    const auto starts = sink->starts;
    sink->Advance(200000);
    window.Tick(.01);
    CHECK(session.Playing() && session.PlaybackEpoch() == epoch + 1);
    CHECK(sink->starts == starts + 1 && sink->offset == 8000 * 4 && sink->elapsed == 0);
    CHECK(session.CursorSeconds() == 1);
    CHECK(session.Seek(60, reason));
    window.Refresh();
    CHECK(!sink->Running());
    CHECK(session.Edit(
        "from-current",
        [](auto& d, auto&) {
            d.timeline.startAtCurrent = 1;
            d.timeline.playbackLoop = 0;
            d.timeline.playStopFrame = 65;
            return true;
        },
        reason));
    window.Dispatch(panel::kPlayButton);
    CHECK(sink->offset == 16000 * 4);
    sink->Advance(200000);
    window.Tick(.01);
    CHECK(!session.Playing() && !sink->Running());
    CHECK(session.Document().timeline.currentFrame == 65);
    sink->fail = true;
    window.Dispatch(panel::kPlayButton);
    CHECK(!session.Playing() && !sink->Running());
    CHECK(window.LastError() == "injected-output-failure");
    sink->fail = false;
    // A device lost during playback pauses at the same frame, retaining the
    // scene and diagnostic. Restarting after recovery establishes a new clock.
    window.Dispatch(panel::kPlayButton);
    CHECK(session.Playing() && sink->Running());
    const auto failedCursor = session.CursorSeconds();
    const auto failedFrames = session.Runtime().Counters().frames;
    sink->deviceFailed = true;
    window.Tick(.25);
    CHECK(!session.Playing() && !sink->Running() && session.CursorSeconds() == failedCursor);
    CHECK(session.Runtime().Counters().frames == failedFrames &&
          window.LastError() == "injected-streaming-device-failure");
    sink->deviceFailed = false;
    CHECK(session.Seek(179 * 30, reason));
    CHECK(session.Edit(
        "audio-end",
        [](auto& d, auto&) {
            d.timeline.playStopFrame = 6000;
            return true;
        },
        reason));
    window.Dispatch(panel::kPlayButton);
    CHECK(sink->Running());
    sink->Advance(1000000);
    window.Tick(.9);
    CHECK(session.CursorSeconds() == 180 && !sink->Running());
    window.Tick(.25);
    CHECK(session.CursorSeconds() == 180.25 && session.Playing());
    window.Dispatch(panel::kPlayButton);
    CHECK(!window.isVisible() && window.RenderWindow()->Submissions() == 0);
    const auto disableAudio = [&]() -> QAction* {
        for (auto* action : window.findChildren<QAction*>())
            if (action->property("mmdCommandId").toInt() == MDS_CMD_TOGGLE_WAVE_DISABLED)
                return action;
        return nullptr;
    };
    CHECK(session.Edit(
        "disabled-wave-range",
        [](auto& d, auto&) {
            d.timeline.playStartFrame = 30;
            d.timeline.playStopFrame = 33;
            d.timeline.startAtCurrent = 0;
            d.timeline.playbackLoop = 1;
            return true;
        },
        reason));
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(code));
        auto* action = disableAudio();
        CHECK(action && action->isEnabled() && !action->isChecked());
        const auto revision = session.Revision(),
                   framesBefore = session.Runtime().Counters().frames;
        const auto beforeStarts = sink->starts;
        action->trigger();
        CHECK(session.PlaybackAudioDisabled() && action->isChecked() &&
              session.Revision() == revision &&
              session.Runtime().Counters().frames == framesBefore);
        sink->fail = true;
        window.Dispatch(panel::kPlayButton);
        CHECK(session.Playing() && !sink->Running() && sink->starts == beforeStarts);
        window.Tick(.05);
        CHECK(std::abs(session.CursorSeconds() - 1.05) < 1e-9);
        window.Dispatch(panel::kPlayButton);
        sink->fail = false;
        action->trigger();
        window.Dispatch(panel::kPlayButton);
        CHECK(sink->Running() && sink->starts == beforeStarts + 1);
        const auto clockEpoch = session.PlaybackEpoch();
        action->trigger(); // Changing the gate leaves current PCM/clock running.
        sink->Advance(150000);
        window.Tick(.001);
        CHECK(session.Playing() && sink->Running() && session.PlaybackEpoch() == clockEpoch + 1 &&
              sink->starts == beforeStarts + 1 && session.CursorSeconds() == 1);
        sink->Advance(10000);
        window.Tick(.02);
        CHECK(std::abs(session.CursorSeconds() - 1.02) < 1e-9);
        action->trigger(); // The next loop can establish a new native clock.
        window.Tick(.1);
        CHECK(sink->starts == beforeStarts + 2 && sink->elapsed == 0 &&
              session.CursorSeconds() == 1 && !session.PlaybackAudioDisabled());
        window.Dispatch(panel::kPlayButton);
    }
    disableAudio()->trigger();
    CHECK(window.Save(qt::Path(dir.path() + "/audio-pref.pmm")) &&
          window.Open(qt::Path(dir.path() + "/audio-pref.pmm")) && session.PlaybackAudioDisabled());
    CHECK(session.New(reason) && session.PlaybackAudioDisabled());
    std::cout
        << "Silent audio transport: PCM offsets, 120s clock/jitter, loop, seek, stop and output failure passed\n";
}
