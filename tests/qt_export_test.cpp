#include "video_encoder.hpp"
#include <QApplication>
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
    for (unsigned clip = 0; clip < 3; ++clip) {
        const auto profile = clip == 1 ? qt::VideoProfile::Matroska : qt::VideoProfile::Mp4;
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
        qt::VideoEncoder encoder;
        const auto target = directory / (clip == 2                          ? "clock-ntsc.mp4"
                                         : profile == qt::VideoProfile::Mp4 ? "clock.mp4"
                                                                            : "clock.mkv");
        CHECK(encoder.Open(target, timeline, profile, &wave, [] { return true; }, reason));
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
                    if (y >= 80 && x < 96)
                        for (unsigned c = 0; c < 3; ++c)
                            frame.rgba[offset + c] = (n & (1u << (x / 16))) ? 240 : 16;
                }
            CHECK(encoder.Write(frame, reason));
        }
        CHECK(encoder.Finish(reason));
        CHECK(std::filesystem::file_size(target) > 1024);
    }
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
    CHECK(!qt::ExportMovie(snapshot, argv[2], options, directory / "scene.mp4", settings,
                           qt::VideoProfile::Mp4, false, {}, reason));
    unsigned callbacks = 0;
    CHECK(!qt::ExportMovie(
        snapshot, argv[2], options, directory / "cancel.mp4", settings, qt::VideoProfile::Mp4,
        false, [&](unsigned, unsigned) { return ++callbacks < 6; }, reason));
    CHECK(reason == "export-cancelled" && !std::filesystem::exists(directory / "cancel.mp4"));
    CHECK(session.Runtime().Counters().frames == count);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        CHECK(entry.path().filename().string().find(".mds-") != 0);
    std::cout
        << "Export: rational clock, bounded video/audio input, trim/silence, same MME source, cancellation and editor isolation passed\n";
}
