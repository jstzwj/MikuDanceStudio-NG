#include "background_video.hpp"
#include "export.hpp"
#include "video_encoder.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <cmath>
#include <iostream>
#include <fstream>
using namespace mikudancestudio;
bool TestHiddenVideo(const std::filesystem::path&, const std::filesystem::path&,
                     const rhi::BackendOptions&);
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
unsigned Number(const rhi::RgbaImage& image) {
    unsigned number = 0;
    for (unsigned bit = 0; bit < 12; ++bit)
        if (image.pixels[(88 * image.width + bit * 12 + 6) * 4] > 128)
            number |= 1u << bit;
    return number;
}
unsigned Number(const QImage& source) {
    const auto image = source.convertToFormat(QImage::Format_RGBA8888);
    unsigned number = 0;
    for (unsigned bit = 0; bit < 12; ++bit)
        if (image.constScanLine(88)[(bit * 12 + 6) * 4] > 128)
            number |= 1u << bit;
    return number;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    QFile manifest(qt::Path(root / "videos.json"));
    CHECK(manifest.open(QIODevice::ReadOnly));
    const auto videos = QJsonDocument::fromJson(manifest.readAll()).array();
    qt::VideoDecoder decoder;
    media::VideoFrame frame;
    for (const auto& entry : videos) {
        const auto video = entry.toObject();
        std::cout << "decode " << video["file"].toString().toStdString() << std::endl;
        CHECK(decoder.Open(root / video["file"].toString().toStdString(), reason));
        CHECK(decoder.Info().width == 160 && decoder.Info().height == 96);
        CHECK(decoder.Read(0, frame, {}, reason));
        // Independent known source colors catch an oracle that generated a
        // malformed clip; comparing two decoders alone could agree on it.
        for (unsigned y : {20u, 60u})
            for (unsigned x : {20u, 100u}) {
                const unsigned offset = (y * 160 + x) * 4;
                const std::array<int, 3> rgb{0, y < 48 ? 40 : 180, x < 80 ? 32 : 224};
                for (unsigned c = 0; c < 3; ++c) {
                    const int actual = frame.image->pixels[offset + c];
                    if (std::abs(actual - rgb[c]) > 8)
                        std::cerr << "source patch " << x << ',' << y << " channel " << c
                                  << ": expected " << rgb[c] << ", got " << actual << '\n';
                    CHECK(std::abs(actual - rgb[c]) <= 8);
                }
            }
        const auto frames = video["frames"].toArray();
        // Forward, backwards, exact boundary, just before boundary, endpoint.
        for (int pass = 0; pass < 2; ++pass)
            for (int n = 0; n < frames.size(); ++n) {
                const int index = pass ? frames.size() - 1 - n : n;
                const auto expected = frames[index].toObject();
                const auto us = expected["ptsUs"].toInteger();
                CHECK(decoder.Read(double(us) / 1000000, frame, {}, reason));
                CHECK(frame.Covers(us) && Number(*frame.image) == expected["number"].toInt());
                if (index && us > 0) {
                    CHECK(decoder.Read(double(us - 1) / 1000000, frame, {}, reason));
                    CHECK(Number(*frame.image) == frames[index - 1].toObject()["number"].toInt());
                }
            }
        CHECK(decoder.Read(10, frame, {}, reason) && frame.last);
        CHECK(Number(*frame.image) == frames.last().toObject()["number"].toInt());
        CHECK(!decoder.Read(.2, frame, [] { return false; }, reason) && !frame.image);
        CHECK(decoder.Read(0, frame, {}, reason) && Number(*frame.image) == 0);
    }
    CHECK(!decoder.Open(root / "missing.mp4", reason));
    CHECK(!decoder.Open(root / "ntsc.mp4", reason, [] { return false; }));
    CHECK(reason == "background-video-cancelled");
    qt::VideoDecoder expired(std::chrono::milliseconds(0));
    CHECK(!expired.Open(root / "ntsc.mp4", reason) && reason == "background-video-timeout");
    CHECK(!decoder.Read(0, frame, {}, reason));
    CHECK(!decoder.Open(root / "triangle.pmx", reason));
    CHECK(decoder.Open(root / "rotated.mp4", reason));
    CHECK(decoder.Info().width == 96 && decoder.Info().height == 160);
    CHECK(decoder.Info().clockwiseRotation == 270);
    CHECK(decoder.Read(0, frame, {}, reason));
    CHECK(frame.image->width == 96 && frame.image->height == 160);
    for (const auto& name : {"rotated.mp4", "wide.mp4"}) {
        CHECK(decoder.Open(root / name, reason) && decoder.Read(0, frame, {}, reason));
        std::ifstream file(root / (std::string(name) + ".rgba"), std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        CHECK(bytes.size() == frame.image->pixels.size());
        double squared = 0;
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            const int delta = int(bytes[n]) - frame.image->pixels[n];
            squared += delta * delta;
        }
        const auto actual = QImage(frame.image->pixels.data(), int(frame.image->width),
                                   int(frame.image->height), QImage::Format_RGBA8888);
        const auto expected =
            QImage(bytes.data(), actual.width(), actual.height(), QImage::Format_RGBA8888);
        CHECK(actual.save(qt::Path(root / (std::string(name) + ".actual.png"))));
        CHECK(expected.save(qt::Path(root / (std::string(name) + ".expected.png"))));
        std::cout << name << " RGBA mean squared error: " << squared / bytes.size() << std::endl;
        CHECK(squared / bytes.size() < 20); // PSNR >35dB, orientation/SAR over every pixel.
    }
#ifdef _WIN32
    QFile reference(qt::Path(root / "vfw-reference.json"));
    CHECK(reference.open(QIODevice::ReadOnly));
    for (const auto& item : QJsonDocument::fromJson(reference.readAll()).array()) {
        const auto clip = item.toObject();
        CHECK(decoder.Open(root / clip["file"].toString().toStdString(), reason));
        for (const auto& value : clip["picks"].toArray()) {
            const auto expected = value.toObject();
            const auto seconds = double(expected["nativeFrame"].toInt()) / 30;
            CHECK(decoder.ReadScene(seconds, true, frame, {}, reason));
            CHECK(Number(*frame.image) == expected["number"].toInt());
        }
    }
#endif
    CHECK(decoder.Open(root / "wide.mp4", reason));
    CHECK(decoder.Info().width == 320 && decoder.Info().height == 96);
    CHECK(decoder.Read(0, frame, {}, reason));
    {
        qt::VideoPreview preview;
        CHECK(preview.Stats().workers == 0);
        for (double time : {1., 0., .8, .2, 1.1, .1})
            preview.Request(root / "ntsc.mp4", time, frame, reason);
        QElapsedTimer timer;
        timer.start();
        qt::VideoReady ready;
        do {
            ready = preview.Request(root / "ntsc.mp4", .1, frame, reason);
            app.processEvents();
            QThread::msleep(1);
        } while (ready == qt::VideoReady::Pending && timer.elapsed() < 10000);
        CHECK(ready == qt::VideoReady::Ready && Number(*frame.image) == 2);
        preview.Clear();
        CHECK(preview.Request(root / "missing.mp4", 0, frame, reason) == qt::VideoReady::Pending);
        timer.restart();
        do {
            ready = preview.Request(root / "missing.mp4", 0, frame, reason);
            QThread::msleep(1);
        } while (ready == qt::VideoReady::Pending && timer.elapsed() < 10000);
        CHECK(ready == qt::VideoReady::Failed);
        preview.Clear();
        timer.restart();
        do {
            ready = preview.Request(root / "ntsc.mp4", .2, frame, reason);
            QThread::msleep(1);
        } while (ready == qt::VideoReady::Pending && timer.elapsed() < 10000);
        CHECK(ready == qt::VideoReady::Ready && Number(*frame.image) == 5);
        for (unsigned cycle = 0; cycle < 24; ++cycle) {
            for (double time : {1., .1, .4, .2})
                preview.Request(root / "ntsc.mp4", time, frame, reason);
            timer.restart();
            do {
                ready = preview.Request(root / "ntsc.mp4", .2, frame, reason);
                QThread::msleep(1);
            } while (ready == qt::VideoReady::Pending && timer.elapsed() < 10000);
            CHECK(ready == qt::VideoReady::Ready);
            const auto live = preview.Stats();
            CHECK(live.workers == 1 && live.openDecoders == 1 && live.inFlight <= 1 &&
                  live.queued <= 1 && live.decoderBytes <= 2 * 160 * 96 * 4 &&
                  live.publishedBytes == 160 * 96 * 4);
            preview.Clear();
            timer.restart();
            while ((preview.Stats().openDecoders || preview.Stats().inFlight) &&
                   timer.elapsed() < 10000)
                QThread::msleep(1);
            const auto released = preview.Stats();
            CHECK(released.decoderBytes == 0 && released.openDecoders == 0 &&
                  released.publishedBytes == 0 && released.queued == 0 && released.inFlight == 0);
        }
    }
    rhi::BackendOptions options;
    const auto api = qEnvironmentVariable("MDS_GPU_BACKEND");
    options.backend = api == "d3d12"    ? rhi::Backend::D3D12
                      : api == "vulkan" ? rhi::Backend::Vulkan
                      : api == "metal"  ? rhi::Backend::Metal
                                        : rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    CHECK(TestHiddenVideo(root, argv[2], options));
    editor::EditorSession session;
    CHECK(session.ImportVideo(root / "ntsc.mp4", 160, 96, 160, 96, reason));
    CHECK(session.Edit(
        "test",
        [](auto& d, auto&) {
            d.view.groundGrid = d.view.groundShadow = 0;
            d.appearance.floorVisible = 0;
            return true;
        },
        reason));
    QImage image;
    for (unsigned n : {0u, 9u, 2u, 45u, 0u}) {
        CHECK(session.Seek(n, reason));
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), argv[2], options, 160, 96, image,
                              reason));
        CHECK(Number(image) == (n ? std::min(179u, unsigned(n * 1000 / 1001)) : 0));
    }
    CHECK(session.Seek(0, reason));
    {
        // A persistent renderer must retire old frame uploads, retain hidden
        // leases, rebuild targets on resize and rebind the same CPU snapshots.
        render::SceneFrameProducer producer;
        rhi::FileAssetSource files(argv[2]);
        CHECK(producer.Load(session.Runtime(), qt::EditorRenderSources(session.Assets()), files,
                            reason));
        auto device = rhi::CreateOffscreenBackendDevice(160, 96, options);
        CHECK(device);
        auto replacement = rhi::CreateOffscreenBackendDevice(160, 96, options);
        CHECK(replacement);
        rhi::SceneRenderer renderer(*device.device);
        CHECK(decoder.Open(root / "ntsc.mp4", reason));
        render::SceneRenderSettings settings;
        settings.width = 160;
        settings.height = 96;
        for (unsigned n = 0; n < 10; ++n) {
            CHECK(session.Seek(n, reason));
            CHECK(decoder.ReadScene(double(n) / 30, true, frame, {}, reason));
            settings.video = frame.image;
            if (n == 3) {
                settings.width = 192;
                settings.height = 128;
            }
            if (n == 4) {
                settings.width = 160;
                settings.height = 96;
            }
            if (n == 5)
                CHECK(session.ShowVideo(false, reason));
            if (n == 6)
                CHECK(session.ShowVideo(true, reason));
            rhi::RenderFrame prepared;
            CHECK(producer.Build(session.Runtime(), settings, prepared, reason));
            CHECK(prepared.passes.front().name == "background_video");
            rhi::PreparedModelFrame gpu;
            rhi::CompletionToken token;
            CHECK(renderer.Prepare(1, prepared, gpu));
            CHECK(renderer.Submit(gpu, token));
            producer.Commit();
            rhi::AttachmentReadback pixels;
            CHECK(renderer.ReadTarget(1, prepared.finalColor, pixels));
            if (n != 3 && n != 5) {
                const QImage output(pixels.bytes.data(), 160, 96, 640, QImage::Format_RGBA8888);
                CHECK(Number(output) == (n ? n - 1 : 0));
            }
            CHECK(renderer.Collect());
            if (n == 4)
                CHECK(renderer.Rebind(*replacement.device));
        }
        const auto live = renderer.Stats();
        CHECK(live.residentTargets > 0 && live.residentResources > 0);
        CHECK(renderer.Unload(1) && renderer.Detach());
        const auto released = renderer.Stats();
        CHECK(released.residentTargets == 0 && released.residentResources == 0 &&
              released.retiringResources == 0 && released.pooledBuffers == 0);
        std::cout << "Video GPU resources before unload: " << live.residentResources
                  << ", targets: " << live.residentTargets
                  << ", retired peak: " << live.peakRetiringResources << std::endl;
        // A screen.bmp consumer reads the same initialized AVI target this
        // frame; its sampled attachment precedes geometry and never feeds back.
        CHECK(session.ImportAccessory(root / "screen.x", reason));
        CHECK(session.Edit(
            "background capture",
            [](auto& d, auto&) {
                d.view.captureMode = 3;
                return true;
            },
            reason));
        CHECK(producer.Load(session.Runtime(), qt::EditorRenderSources(session.Assets()), files,
                            reason));
        rhi::RenderFrame captured;
        CHECK(producer.Build(session.Runtime(), settings, captured, reason));
        bool consumer = false;
        for (const auto& pass : captured.passes)
            for (const auto& draw : pass.draws)
                if (draw.sceneObject && draw.targetTextures[0].target == 6)
                    consumer = true;
        CHECK(consumer);
        rhi::PreparedModelFrame gpu;
        rhi::CompletionToken token;
        CHECK(renderer.Prepare(1, captured, gpu) && renderer.Submit(gpu, token));
        CHECK(renderer.Unload(1) && renderer.Collect());
        CHECK(renderer.Stats().residentResources == 0);
        CHECK(session.UnloadAccessory(0, reason));
        CHECK(session.Edit(
            "capture off",
            [](auto& d, auto&) {
                d.view.captureMode = 0;
                return true;
            },
            reason));
    }
    CHECK(session.Seek(0, reason));
    CHECK(session.Save(
        root / "video.pmm",
        [](const auto& path, const auto& bytes, auto&) {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return bool(file);
        },
        reason));
    CHECK(session.New(reason) && session.Assets().videoPath.empty());
    CHECK(session.Open(root / "video.pmm", reason));
    CHECK(session.Assets().videoPath == root / "ntsc.mp4" && session.Document().avi.enabled);
    CHECK(qt::ExportSequence(session, argv[2], options, root / "video-sequence", 0, 5, 160, 96, {},
                             reason));
    for (unsigned n = 0; n < 6; ++n) {
        const auto path =
            qt::Path(root / "video-sequence") + QString("/frame_%1.png").arg(n, 8, 10, QChar('0'));
        CHECK(Number(QImage(path)) == (n ? n - 1 : 0));
    }
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    CHECK(session.Edit(
        "carrier",
        [](auto& d, auto&) {
            d.accessories.front().state.position = {1000, 1000, 1000};
            return true;
        },
        reason));
    std::filesystem::copy(MDS_EDITOR_EFFECTS_DIR, root / "effects",
                          std::filesystem::copy_options::recursive);
    CHECK(session.AssignEffect({false, 0, -1}, root / "effects/Material.fx", reason));
    CHECK(session.AssignEffect({true, 0, -1}, root / "effects/Scene.fx", reason));
    auto snapshot = render::CaptureExport(session);
    render::ExportSettings spec;
    spec.width = 160;
    spec.height = 96;
    spec.last = 5;
    CHECK(qt::ExportImages(snapshot, argv[2], options, root / "mme-sequence", spec, {}, reason));
    CHECK(Number(QImage(qt::Path(root / "mme-sequence/frame_00000005.png"))) == 4);
    spec.rateNumerator = 60;
    CHECK(qt::ExportImages(snapshot, argv[2], options, root / "mme-60", spec, {}, reason));
    spec.alpha = render::ExportAlpha::Opaque;
    CHECK(qt::ExportMovie(snapshot, argv[2], options, root / "background.mp4", spec,
                          qt::VideoProfile::Mp4, false, {}, reason));
    CHECK(!qt::ExportImages(
        snapshot, argv[2], options, root / "cancel-sequence", spec,
        [](unsigned n, unsigned) { return n < 1; }, reason));
    CHECK(!std::filesystem::exists(root / "cancel-sequence"));
    auto broken = snapshot;
    broken.assets.videoPath = root / "missing.mp4";
    CHECK(!qt::ExportImages(broken, argv[2], options, root / "broken-sequence", spec, {}, reason));
    CHECK(!std::filesystem::exists(root / "broken-sequence"));
    CHECK(session.ShowVideo(false, reason));
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), argv[2], options, 192, 128, image,
                          reason));
    CHECK(session.ShowVideo(true, reason));
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), argv[2], options, 192, 128, image,
                          reason));
    qt::EditorWindow window(argv[2], true);
    CHECK(window.Open(root / "video.pmm"));
    window.RenderWindow()->SetOptions(options);
    QElapsedTimer timer;
    timer.start();
    while (!window.RenderWindow()->PrepareFrame(160, 96, reason) &&
           reason == "background-video-pending" && timer.elapsed() < 10000)
        QThread::msleep(1);
    CHECK(reason.empty());
    CHECK(window.SetLanguage("ja") && window.SetLanguage("zh_CN") && window.SetLanguage("en"));
    QAction* toggle = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->property("mmdCommandId").toInt() == MDS_CMD_TOGGLE_BACKGROUND_VIDEO)
            toggle = action;
    CHECK(toggle && toggle->isEnabled());
    toggle->trigger();
    CHECK(!window.Session().Document().avi.enabled);
    toggle->trigger();
    CHECK(window.Session().Document().avi.enabled && !window.isVisible());
    CHECK(window.Session().Play(reason));
    window.Tick(.2);
    CHECK(window.Session().CursorSeconds() > 0);
    window.Session().Pause();
    CHECK(window.Session().Seek(3, reason));
    timer.restart();
    while (!window.RenderWindow()->PrepareFrame(160, 96, reason) &&
           reason == "background-video-pending" && timer.elapsed() < 10000)
        QThread::msleep(1);
    CHECK(reason.empty());
    const auto updates = window.Session().Runtime().Counters().frames;
    window.RenderWindow()->SetOptions(options);
    CHECK(window.RenderWindow()->PrepareFrame(192, 128, reason));
    CHECK(window.Session().Runtime().Counters().frames == updates);
    CHECK(window.Session().New(reason));
    CHECK(window.RenderWindow()->PrepareFrame(160, 96, reason));
    for (const auto& pass : window.RenderWindow()->PreparedFrame().passes)
        CHECK(pass.name != "background_video");
    for (const auto& file : std::filesystem::directory_iterator(root))
        CHECK(file.path().filename().u8string().find(".mds-") != 0);
    std::cout
        << "background video PTS, seek, rotation/SAR, scene/MME/export, cancellation and hidden Qt passed\n";
    return 0;
}
