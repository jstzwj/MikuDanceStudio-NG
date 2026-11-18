#include "editor_window.hpp"
#include "video_encoder.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <Windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

using namespace mikudancestudio;
namespace {
std::uint64_t ResidentBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)
               ? counters.WorkingSetSize
               : 0;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                     &count) == KERN_SUCCESS
               ? info.resident_size
               : 0;
#else
    std::ifstream statm("/proc/self/statm");
    std::uint64_t size = 0, resident = 0;
    statm >> size >> resident;
    return resident * std::uint64_t(sysconf(_SC_PAGESIZE));
#endif
}

class SilentOutput : public qt::AudioOutput {
public:
    bool Start(const QAudioFormat& format, QIODevice& source, float, std::string&) override {
        char samples[64];
        running = format.sampleRate() == 48000 && source.read(samples, sizeof samples) == 64;
        processed = 0;
        return running;
    }
    void Stop() override { running = false; }
    void Volume(float) override {}
    bool Running() const override { return running; }
    qint64 ProcessedMicroseconds() const override { return processed; }
    qint64 processed = 0;
    bool running = false;
};
}

bool TestMediaRecovery(const std::filesystem::path& root, const std::filesystem::path& builtins,
                       const rhi::BackendOptions& options, const render::ExportSnapshot& original) {
    std::string reason;
#define VERIFY(x)                                                                                  \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "Combined media " << __LINE__ << ": " << #x << ": " << reason << '\n';    \
            return false;                                                                          \
        }                                                                                          \
    } while (false)
    const auto path = root / "combined-source.mp4";
    const auto away = root / "combined-away.mp4";
    auto snapshot = original;
    snapshot.assets.wave = root / "long-combined.wav";
    snapshot.assets.videoPath = path;
    VERIFY(std::filesystem::file_size(snapshot.assets.wave) > 64 * 1024 * 1024);
    std::filesystem::copy_file(root / "ntsc.mp4", path);
    auto silent = std::make_unique<SilentOutput>();
    auto* sink = silent.get();
    qt::EditorWindow editor(builtins, true, std::move(silent));
    auto& session = editor.Session();
    auto& view = *editor.RenderWindow();
    view.SetOptions(options);
    VERIFY(editor.Open(root / "video.pmm"));
    VERIFY(session.ImportModel(root / "triangle.pmx", reason));
    VERIFY(session.ImportAccessory(root / "bound.x", reason));
    VERIFY(session.AssignEffect({false, 0, -1}, root / "effects/Material.fx", reason));
    VERIFY(session.AssignEffect({true, 0, -1}, root / "effects/Scene.fx", reason));
    VERIFY(session.ImportWave(snapshot.assets.wave, reason));
    VERIFY(session.ImportVideo(path, 160, 96, 160, 96, reason));
    VERIFY(session.Edit(
        "loop",
        [](auto& d, auto&) {
            d.timeline.playStartFrame = 0;
            d.timeline.playStopFrame = 4;
            d.timeline.startAtCurrent = 0;
            d.timeline.playbackLoop = 1;
            return true;
        },
        reason));
    auto prepare = [&] {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!view.PrepareFrame(160, 96, reason)) {
            if (reason != "background-video-pending" || elapsed.elapsed() >= 10000)
                return false;
            QThread::msleep(1);
        }
        return true;
    };
    // Source loss after import and before the first preview open.
    std::filesystem::rename(path, away);
    editor.Dispatch(panel::kPlayButton);
    VERIFY(session.Playing());
    sink->processed = 100000;
    editor.Tick(.01);
    VERIFY(!prepare() && reason == "background-video-file-missing");
    const auto cursor = session.CursorSeconds();
    const auto frames = session.Runtime().Counters().frames;
    editor.Tick(.9);
    VERIFY(!session.Playing() && session.CursorSeconds() == cursor && !sink->running);
    VERIFY(session.Runtime().Counters().frames == frames);
    std::filesystem::rename(away, path);
    editor.Dispatch(panel::kPlayButton);
    VERIFY(session.Playing() && session.CursorSeconds() == cursor && prepare());
    sink->processed = 100000;
    editor.Tick(.01);
    VERIFY(session.Playing() && session.CursorSeconds() == 0 && prepare());
    VERIFY(session.ShowVideo(false, reason));
    const auto frozen = session.Assets().video.frozen.image;
    VERIFY(frozen);
    sink->running = false;
    session.SetEditMode(1); // Native user stop would refresh the frozen frame.
    editor.Tick(.3);
    VERIFY(!session.Playing() && session.Assets().video.frozen.image == frozen);
    for (unsigned frame : {31, 2, 48, 1, 0})
        VERIFY(session.Seek(frame, reason) && prepare());
    if (rhi::BackendAvailable(rhi::Backend::D3D11) && rhi::BackendAvailable(rhi::Backend::D3D12)) {
        auto second = options;
        second.backend =
            options.backend == rhi::Backend::D3D12 ? rhi::Backend::D3D11 : rhi::Backend::D3D12;
        view.SetOptions(second);
        VERIFY(prepare());
        QImage image;
        VERIFY(qt::RenderImage(session.Runtime(), session.Assets(), builtins, second, 160, 96,
                               image, reason));
    }
    view.SetOptions(options);
    VERIFY(prepare() && session.Assets().video.frozen.image == frozen);
    VERIFY(session.New(reason) && prepare());
    VERIFY(!editor.isVisible() && view.Submissions() == 0);

    render::ExportSettings spec;
    spec.width = 160;
    spec.height = 96;
    spec.last = 5;
    spec.alpha = render::ExportAlpha::Opaque;
    const auto target = root / "combined-retry.mp4";
    auto exportVideo = [&](const render::ExportSnapshot& input,
                           const qt::ExportProgress& progress) {
        return qt::ExportMovie(input, builtins, options, target, spec, qt::VideoProfile::Mp4Pcm,
                               true, progress, reason);
    };
    const auto cancel = [](unsigned done, unsigned) { return done < 1; };
    VERIFY(!exportVideo(snapshot, cancel) && reason == "export-cancelled");
    const auto initial = ResidentBytes();
    for (unsigned cycle = 0; cycle < 8; ++cycle)
        VERIFY(!exportVideo(snapshot, cancel) && reason == "export-cancelled");
    const auto afterCancels = ResidentBytes();
    VERIFY(initial > 0 && afterCancels > 0 && afterCancels <= initial + 96 * 1024 * 1024);
    auto broken = snapshot;
    broken.assets.videoPath = root / "missing-combined.mp4";
    VERIFY(!exportVideo(broken, {}) && reason == "background-video-file-missing");
    VERIFY(!std::filesystem::exists(target) && exportVideo(snapshot, {}));

    spec.alpha = render::ExportAlpha::Straight;
    VERIFY(qt::ExportImages(snapshot, builtins, options, root / "combined-png", spec, {}, reason));
    for (unsigned frame = 0; frame < 6; ++frame) {
        const auto name = QString("frame_%1.png").arg(frame, 8, 10, QChar('0'));
        const QImage expected(qt::Path(root / "mme-sequence") + "/" + name);
        const QImage result(qt::Path(root / "combined-png") + "/" + name);
        VERIFY(!expected.isNull() && expected == result);
    }
    QFile report(qt::Path(root / "media-budget.json"));
    const auto data = QJsonDocument(QJsonObject{{"cancelCycles", 9},
                                                {"residentBefore", double(initial)},
                                                {"residentAfter", double(afterCancels)},
                                                {"rssGrowthLimitBytes", 96 * 1024 * 1024},
                                                {"exactPngFrames", 6}})
                          .toJson();
    VERIFY(report.open(QIODevice::WriteOnly) && report.write(data) == data.size());
    return true;
}
