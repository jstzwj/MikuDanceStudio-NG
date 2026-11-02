#include "audio.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include <QApplication>
#include <QAudioDevice>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>

// This opt-in test opens a real platform audio device, feeding only float zero.
// The ordinary offscreen suite never opens an audio device. Missing devices are
// reported as unavailable (77), never as a successful native acceptance.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (!args.contains("--native-silent")) {
        std::cerr << "Explicit --native-silent is required\n";
        return 2;
    }
    const auto option = [&](const QString& name, const QString& fallback) {
        const auto index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1] : fallback;
    };
    bool valid = false;
    const int seconds = option("--seconds", "12").toInt(&valid);
    if (!valid || seconds < 3 || seconds > 120)
        return 2;
    const auto reportPath = option("--report", "native-audio.json");
    QJsonObject report{{"status", "failed"}, {"seconds", seconds}, {"silent", true}};
    const auto finish = [&](int code, const std::string& message) {
        report["status"] = code == 0 ? "passed" : code == 77 ? "unavailable" : "failed";
        report["detail"] = QString::fromStdString(message);
        QFile file(reportPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) {
            std::cerr << "Could not write native audio report\n";
            return 1;
        }
        std::cout << QJsonDocument(report).toJson().constData();
        return code;
    };
    const auto device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return finish(77, "No native audio output device");
    auto format = device.preferredFormat();
    format.setSampleFormat(QAudioFormat::Float);
    report["device"] = device.description();
    report["sampleRate"] = format.sampleRate();
    report["channels"] = format.channelCount();
    if (!device.isFormatSupported(format))
        return finish(1, "Preferred-rate float PCM is unsupported");
    QTemporaryDir temporary;
    QFile wave(temporary.path() + "/silence.wav");
    if (!temporary.isValid() || !wave.open(QIODevice::WriteOnly))
        return finish(1, "Cannot create silent WAV");
    const quint32 frames = (seconds + 3) * format.sampleRate();
    const quint32 size = frames * format.channelCount() * 4;
    QDataStream out(&wave);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + size);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(3) << quint16(format.channelCount())
        << quint32(format.sampleRate()) << quint32(format.bytesPerFrame() * format.sampleRate())
        << quint16(format.bytesPerFrame()) << quint16(32);
    out.writeRawData("data", 4);
    out << size;
    const QByteArray silence(size, '\0');
    if (out.writeRawData(silence.constData(), silence.size()) != silence.size())
        return finish(1, "Cannot write silent WAV");
    wave.close();
    mikudancestudio::qt::AudioTransport transport;
    std::string reason;
    if (!transport.Load(mikudancestudio::qt::Path(wave.fileName()), reason))
        return finish(1, reason);
    QElapsedTimer elapsed;
    elapsed.start();
    if (!transport.Play(.25, reason))
        return finish(1, reason);
    report["startupMilliseconds"] = double(elapsed.elapsed());
    if (elapsed.elapsed() > 2000)
        return finish(1, "Native output initialization exceeded two seconds");
    // Device activation is synchronous and precedes sample playback. Measure
    // that separately instead of counting initialization as missing audio.
    elapsed.restart();
    double previous = .25, maximumLag = 0, maximumLead = 0;
    int advancing = 0;
    while (elapsed.elapsed() < seconds * 1000) {
        app.processEvents();
        const auto position = transport.ClockPosition();
        if (!transport.Error().empty())
            return finish(1, transport.Error());
        const auto wall = elapsed.nsecsElapsed() / 1e9;
        if (!position || !std::isfinite(*position) || *position < previous)
            return finish(1, "Native audio clock ended early or moved backwards");
        if (*position > previous)
            ++advancing;
        previous = *position;
        const auto lag = wall - (*position - .25);
        maximumLag = std::max(maximumLag, lag);
        maximumLead = std::max(maximumLead, -lag);
        report["wallSeconds"] = wall;
        report["positionSeconds"] = *position;
        report["maximumLagSeconds"] = maximumLag;
        report["maximumLeadSeconds"] = maximumLead;
        if (wall > 2 && (lag > .1 || lag < -.1))
            return finish(1, "Native clock differs from elapsed time by over 100 ms");
        QThread::msleep(5);
    }
    if (advancing < seconds * 5)
        return finish(1, "Native audio clock did not advance regularly");
    report["maximumLagSeconds"] = maximumLag;
    report["maximumLeadSeconds"] = maximumLead;
    report["advancingSamples"] = advancing;
    transport.Stop();
    if (transport.Running() || transport.ClockPosition())
        return finish(1, "Stop retained a live native clock");
    if (!transport.Play(1.5, reason))
        return finish(1, reason);
    const auto restarted = transport.ClockPosition();
    if (!restarted || *restarted < 1.5 || *restarted > 1.55)
        return finish(1, "Seek/restart retained the previous native clock");
    transport.Volume(.25f);
    const double duration = seconds + 3;
    if (!transport.Play(duration - .15, reason))
        return finish(1, reason);
    elapsed.restart();
    double last = transport.Position();
    bool ended = false;
    while (elapsed.elapsed() < 2000) {
        app.processEvents();
        const auto position = transport.ClockPosition();
        if (!position) {
            ended = true;
            break;
        }
        if (*position < last || *position > duration + .02)
            return finish(1, "Native EOF clock is outside the WAV range");
        last = *position;
        QThread::msleep(5);
    }
    report["endPositionSeconds"] = last;
    if (!ended || std::abs(last - duration) > .02)
        return finish(1, "Native EOF did not hand off the final processed WAV position");
    transport.Stop();

    // Exercise the actual editor's clock selection with intentionally jittery
    // UI ticks. No window is shown and no viewport frame is submitted.
    mikudancestudio::qt::EditorWindow window(
        mikudancestudio::qt::Path(option("--assets", "res/assets")), true,
        mikudancestudio::qt::CreateAudioOutput());
    auto& session = window.Session();
    if (!session.ImportWave(mikudancestudio::qt::Path(wave.fileName()), reason) ||
        !session.Edit(
            "native-audio-range",
            [](auto& document, auto&) {
                document.timeline.playStartFrame = 30;
                document.timeline.playStopFrame = 10000;
                document.timeline.startAtCurrent = 0;
                document.timeline.playbackLoop = 0;
                return true;
            },
            reason))
        return finish(1, reason);
    window.Dispatch(mikudancestudio::panel::kPlayButton);
    if (!session.Playing())
        return finish(1, window.LastError().toStdString());
    elapsed.restart();
    unsigned ticks = 0;
    double frameError = 0;
    while (elapsed.elapsed() < 3000) {
        app.processEvents();
        window.Tick(ticks % 2 ? .001 : .9);
        const auto wall = elapsed.nsecsElapsed() / 1e9;
        frameError = std::max(frameError, std::abs(session.CursorSeconds() - 1 - wall) * 30);
        if (!session.Playing() || !window.LastError().isEmpty() || frameError > 3)
            return finish(1, "Editor did not follow the native audio clock within three frames");
        ++ticks;
        QThread::msleep(ticks % 2 ? 7 : 31);
    }
    window.Dispatch(mikudancestudio::panel::kPlayButton);
    const auto stopped = session.CursorSeconds();
    window.Tick(.1);
    if (session.Playing() || session.CursorSeconds() != stopped || window.isVisible() ||
        window.RenderWindow()->Submissions())
        return finish(1, "Editor stop advanced playback or exposed a native window");
    report["editorMaximumFrameError"] = frameError;
    report["editorJitteredTicks"] = int(ticks);
    return finish(0, "Native silent PCM, processed clock, stop, seek/restart and EOF passed");
}
