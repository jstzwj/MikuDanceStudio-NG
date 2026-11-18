#include "video_encoder.hpp"
#include "export_dialog.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <cmath>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    render::ExportSettings settings;
    render::ExportTimeline timeline;
    settings.first = 30;
    settings.last = 59;
    settings.width = 160;
    settings.height = 96;
    settings.rateNumerator = 30000;
    settings.rateDenominator = 1001;
    CHECK(render::MakeExportTimeline(settings, timeline, reason));
    CHECK(timeline.frames == 30 && timeline.TimestampUs(30) == 1001000);
    CHECK(timeline.AudioSample(30, 48000) == 48048 && timeline.SourceAudioSample(48000) == 48000);
    CHECK(std::abs(timeline.SourceSeconds(0) - 1) < 1e-12);
    auto invalid = settings;
    invalid.rateDenominator = 0;
    CHECK(!render::MakeExportTimeline(invalid, timeline, reason));
    CHECK(timeline.frames == 30);
    invalid = settings;
    invalid.width = 8192;
    invalid.height = 8192;
    CHECK(!render::MakeExportTimeline(invalid, timeline, reason));
    settings.first = 0;
    settings.last = 29;
    settings.rateNumerator = 60;
    settings.rateDenominator = 1;
    settings.alpha = render::ExportAlpha::Opaque;
    CHECK(render::MakeExportTimeline(settings, timeline, reason) && timeline.frames == 60);
    const auto directory =
        qEnvironmentVariable("MDS_EXPORT_EVIDENCE").isEmpty()
            ? root
            : std::filesystem::u8path(
                  qEnvironmentVariable("MDS_EXPORT_EVIDENCE").toUtf8().constData());
    std::filesystem::create_directories(directory);
    format::WaveAudio wave;
    wave.channels = 2;
    wave.sampleRate = 48000;
    // The first source second is skipped. Pulse at 1.25s must land at 0.25s;
    // source ends at 1.5s, so the latter half of output must be silence.
    wave.samples.resize(72000 * 2);
    for (unsigned n = 60000; n < 60480; ++n) {
        wave.samples[n * 2] = .75f;
        wave.samples[n * 2 + 1] = -.5f;
    }
    settings.first = 30;
    settings.last = 59;
    CHECK(render::MakeExportTimeline(settings, timeline, reason));
    for (unsigned clip = 0; clip < 4; ++clip) {
        const auto profile = (clip & 1) ? qt::VideoProfile::Mp4Pcm : qt::VideoProfile::Mp4;
        if (clip == 2) {
            settings.last = 3629;
            settings.rateNumerator = 30000;
            settings.rateDenominator = 1001;
            CHECK(render::MakeExportTimeline(settings, timeline, reason));
            wave.samples.resize(5784000 * 2); // Source ends at 120.5 s.
            for (unsigned n = 5772000; n < 5772480; ++n) {
                wave.samples[n * 2] = .75f;
                wave.samples[n * 2 + 1] = -.5f;
            }
        }
        // Exercise the actual file-backed source beyond the old 64 MiB cap
        // and at a one-hour project offset, retaining the 120s pulse oracle.
        std::shared_ptr<const format::WaveSource> streamed;
        if (clip == 3) {
            settings.first = 108030;
            settings.last = settings.first + 3599;
            CHECK(render::MakeExportTimeline(settings, timeline, reason));
            const auto wav = root / "long-export.wav";
            constexpr std::uint64_t frames = 48000ull * 3720 + 24000;
            std::ofstream out(wav, std::ios::binary);
            const auto u32 = [&](std::uint32_t n) {
                for (unsigned c = 0; c < 4; ++c)
                    out.put(char(n >> (c * 8)));
            };
            out.write("RIFF", 4);
            u32(unsigned(36 + frames * 4));
            out.write("WAVEfmt ", 8);
            u32(16);
            out.put(1);
            out.put(0);
            out.put(2);
            out.put(0);
            u32(48000);
            u32(192000);
            out.put(4);
            out.put(0);
            out.put(16);
            out.put(0);
            out.write("data", 4);
            u32(unsigned(frames * 4));
            std::array<char, 65536> silence{};
            for (auto left = frames * 4; left;) {
                const auto bytes = std::min<std::uint64_t>(left, silence.size());
                out.write(silence.data(), std::streamsize(bytes));
                left -= bytes;
            }
            for (auto offset : {48000ull * 3601 + 12000, 48000ull * 3720 + 12000}) {
                out.seekp(std::streamoff(44 + offset * 4));
                for (unsigned n = 0; n < 480; ++n) {
                    out.put(0);
                    out.put(0x60);
                    out.put(0);
                    out.put(char(0xc0));
                }
            }
            CHECK(out);
            out.close();
            CHECK(format::OpenWaveFile(wav, streamed, reason));
        }
        qt::VideoEncoder encoder;
        const auto target = directory / (clip == 2                          ? "clock-ntsc.mp4"
                                         : clip == 3                        ? "clock-pcm-ntsc.mp4"
                                         : profile == qt::VideoProfile::Mp4 ? "clock.mp4"
                                                                            : "clock-pcm.mp4");
        CHECK(encoder.Open(
            target, timeline, profile, streamed ? streamed.get() : &wave, [] { return true; },
            reason));
        for (unsigned n = 0; n < timeline.frames; ++n) {
            render::ExportFrame frame{n, timeline.TimestampUs(n), timeline.TimestampUs(n + 1), {}};
            frame.rgba.resize(160 * 96 * 4);
            for (unsigned y = 0; y < 96; ++y)
                for (unsigned x = 0; x < 160; ++x) {
                    const auto offset = (y * 160 + x) * 4;
                    frame.rgba[offset] = std::uint8_t(n * 3);
                    frame.rgba[offset + 1] = std::uint8_t(y * 2);
                    frame.rgba[offset + 2] = x < 80 ? 32 : 224;
                    frame.rgba[offset + 3] = 255;
                    if (y >= 80 && x < 144)
                        for (unsigned c = 0; c < 3; ++c)
                            frame.rgba[offset + c] = (n & (1u << (x / 12))) ? 240 : 16;
                }
            CHECK(encoder.Write(frame, reason));
        }
        CHECK(encoder.Finish(reason));
        CHECK(std::filesystem::file_size(target) > 1024);
    }
    settings.first = 30;
    settings.last = 3629;
    rhi::BackendOptions options;
    const auto api = qEnvironmentVariable("MDS_GPU_BACKEND");
    options.backend = api == "d3d12"    ? rhi::Backend::D3D12
                      : api == "vulkan" ? rhi::Backend::Vulkan
                      : api == "metal"  ? rhi::Backend::Metal
                                        : rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    editor::EditorSession session;
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    std::filesystem::copy(MDS_EDITOR_EFFECTS_DIR, root / "effects",
                          std::filesystem::copy_options::recursive);
    CHECK(session.AssignEffect({false, 0, -1}, root / "effects/Material.fx", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    auto placement = session.Document().accessories[0].state;
    placement.position[0] = 10000;
    CHECK(session.SetAccessory(0, placement, reason));
    CHECK(session.Register({{editor::Track::Accessory, 0, 0, 0}}, reason));
    CHECK(session.AssignEffect({true, 0, -1}, root / "effects/Scene.fx", reason));
    auto snapshot = render::CaptureExport(session);
    const auto count = session.Runtime().Counters().frames;
    settings.first = 0;
    settings.last = 2;
    settings.rateNumerator = 30;
    settings.rateDenominator = 1;
    CHECK(
        qt::ExportImages(snapshot, argv[2], options, directory / "sequence", settings, {}, reason));
    CHECK(qt::ExportMovie(snapshot, argv[2], options, directory / "scene.mp4", settings,
                          qt::VideoProfile::Mp4, false, {}, reason));
    auto faster = settings;
    faster.rateNumerator = 60;
    CHECK(qt::ExportImages(snapshot, argv[2], options, directory / "sequence-60", faster, {},
                           reason));
    CHECK(std::filesystem::exists(directory / "sequence-60/frame_00000005.png") &&
          !std::filesystem::exists(directory / "sequence-60/frame_00000006.png"));
    CHECK(!qt::ExportMovie(snapshot, argv[2], options, directory / "scene.mp4", settings,
                           qt::VideoProfile::Mp4, false, {}, reason));
    unsigned callbacks = 0;
    CHECK(!qt::ExportMovie(
        snapshot, argv[2], options, directory / "cancel.mp4", settings, qt::VideoProfile::Mp4,
        false,
        [&](unsigned completed, unsigned) {
            ++callbacks;
            return completed < 1;
        },
        reason));
    CHECK(reason == "export-cancelled" && !std::filesystem::exists(directory / "cancel.mp4"));
    CHECK(callbacks > 1);
    CHECK(!qt::ExportMovie(snapshot, argv[2], options, directory / "wrong.mov", settings,
                           qt::VideoProfile::Mp4, false, {}, reason));
    CHECK(reason == "export-video-extension-mismatch");
    auto broken = snapshot;
    broken.assets.wave = root / "missing.wav";
    CHECK(!qt::ExportMovie(broken, argv[2], options, directory / "missing-wave.mp4", settings,
                           qt::VideoProfile::Mp4, true, {}, reason));
    CHECK(reason == "wave-file-read-failed");
    {
        // Metadata is intentionally cheap to open. A damaged float sample is
        // rejected when consumed, and must still clean the staged movie.
        const auto invalid = root / "invalid-float.wav";
        std::ofstream file(invalid, std::ios::binary);
        const auto u32 = [&](std::uint32_t n) {
            for (unsigned c = 0; c < 4; ++c)
                file.put(char(n >> (c * 8)));
        };
        file.write("RIFF", 4);
        u32(40);
        file.write("WAVEfmt ", 8);
        u32(16);
        file.put(3);
        file.put(0);
        file.put(1);
        file.put(0);
        u32(48000);
        u32(192000);
        file.put(4);
        file.put(0);
        file.put(32);
        file.put(0);
        file.write("data", 4);
        u32(4);
        u32(0x7fc00000);
        file.close();
        broken.assets.wave = invalid;
        CHECK(!qt::ExportMovie(broken, argv[2], options, directory / "invalid-float.mp4", settings,
                               qt::VideoProfile::Mp4, true, {}, reason));
        CHECK(reason == "nonfinite-wave-sample" &&
              !std::filesystem::exists(directory / "invalid-float.mp4"));
    }
    {
        std::ofstream file(root / "effects/broken.fx");
        file << "not an effect";
    }
    CHECK(session.AssignEffect({false, 0, -1}, root / "effects/broken.fx", reason));
    broken = render::CaptureExport(session);
    CHECK(!qt::ExportMovie(broken, argv[2], options, directory / "broken.mp4", settings,
                           qt::VideoProfile::Mp4, false, {}, reason));
    CHECK(!reason.empty() && !std::filesystem::exists(directory / "broken.mp4"));
    CHECK(session.AssignEffect({false, 0, -1}, root / "effects/Material.fx", reason));
    CHECK(session.Runtime().Counters().frames == count);
    // Export owns a frozen scene while UI events and edits continue on the
    // main thread. The worker must not borrow widgets or the live session.
    qt::ExportTask worker;
    worker.snapshot = snapshot;
    worker.settings = settings;
    worker.backend = options;
    worker.builtins = argv[2];
    worker.destination = directory / "worker.mp4";
    worker.format = 1;
    worker.audio = false;
    unsigned heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start();
    worker.start();
    CHECK(session.UnloadModel(0, reason));
    const auto afterUnload = session.Runtime().Counters().frames;
    QElapsedTimer deadline;
    deadline.start();
    while (worker.isRunning() && deadline.elapsed() < 30000) {
        app.processEvents();
        QThread::msleep(1);
    }
    CHECK(worker.wait(1000) && worker.success);
    CHECK(heartbeats > 0 && session.Runtime().Counters().frames == afterUnload);
    heartbeat.stop();
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    qt::Language language;
    qt::ExportDialog dialog(session, language, argv[2], [&] { return options; }, nullptr);
    dialog.SelectVideo(true);
    CHECK(dialog.findChild<QComboBox*>("export.format")->currentIndex() == 1);
    dialog.SelectVideo(false);
    for (const auto& item : qt::Language::Available()) {
        CHECK(language.Set(item.code));
        dialog.Retranslate();
        CHECK(dialog.windowTitle() != "export.title");
    }
    dialog.findChild<QLineEdit*>("export.destination")
        ->setText(qt::Path(directory / "dialog-sequence"));
    dialog.findChild<QSpinBox*>("export.range.last")->setValue(2);
    dialog.findChild<QSpinBox*>("export.size.first")->setValue(160);
    dialog.findChild<QSpinBox*>("export.size.last")->setValue(96);
    auto* rate = dialog.findChild<QComboBox*>("export.rate");
    rate->setEditText("30/1/2");
    dialog.findChild<QPushButton*>("export.start")->click();
    CHECK(!dialog.Running());
    rate->setEditText("30");
    dialog.findChild<QPushButton*>("export.start")->click();
    CHECK(dialog.Running());
    deadline.restart();
    while ((dialog.Running() || !dialog.findChild<QPushButton*>("export.start")->isEnabled()) &&
           deadline.elapsed() < 30000) {
        app.processEvents();
        QThread::msleep(1);
    }
    CHECK(!dialog.Running() &&
          std::filesystem::exists(directory / "dialog-sequence/sequence.json"));
    CHECK(!dialog.isVisible());
    // Closing a running dialog cancels its worker before destruction. The
    // next attempt must succeed using the same UI, without partial output.
    dialog.findChild<QLineEdit*>("export.destination")->setText(qt::Path(directory / "closed"));
    dialog.findChild<QSpinBox*>("export.range.last")->setValue(2999);
    dialog.findChild<QPushButton*>("export.start")->click();
    CHECK(dialog.Running());
    dialog.close();
    deadline.restart();
    while ((dialog.Running() || !dialog.findChild<QPushButton*>("export.start")->isEnabled()) &&
           deadline.elapsed() < 30000) {
        app.processEvents();
        QThread::msleep(1);
    }
    CHECK(!dialog.Running() && !std::filesystem::exists(directory / "closed"));
    dialog.findChild<QLineEdit*>("export.destination")->setText(qt::Path(directory / "recovered"));
    dialog.findChild<QSpinBox*>("export.range.last")->setValue(0);
    dialog.findChild<QPushButton*>("export.start")->click();
    deadline.restart();
    while (dialog.Running() && deadline.elapsed() < 30000) {
        app.processEvents();
        QThread::msleep(1);
    }
    CHECK(!dialog.Running() && std::filesystem::exists(directory / "recovered/sequence.json"));
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        CHECK(entry.path().filename().string().find(".mds-") != 0);
    std::cout
        << "Export: rational clock, bounded video/audio input, trim/silence, same MME source, cancellation and editor isolation passed\n";
}
