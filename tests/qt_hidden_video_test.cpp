#include "background_video.hpp"
#include "export.hpp"
#include "video_encoder.hpp"
#include "viewport.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <iostream>

using namespace mikudancestudio;
namespace {
unsigned Number(const rhi::RgbaImage& image) {
    unsigned number = 0;
    const unsigned y = unsigned(88.5 * image.height / 96);
    for (unsigned bit = 0; bit < 12; ++bit) {
        const auto x = unsigned((bit * 12 + 6.5) * image.width / 160);
        if (image.pixels[(std::size_t(y) * image.width + x) * 4] > 128)
            number |= 1u << bit;
    }
    return number;
}
bool Wait(qt::Viewport& viewport, std::string& reason, unsigned width = 160) {
    QElapsedTimer timer;
    timer.start();
    while (!viewport.PrepareFrame(width, 96, reason)) {
        if (reason != "background-video-pending" || timer.elapsed() > 10000)
            return false;
        QThread::msleep(1);
    }
    return true;
}
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string&) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return bool(file);
}
}

// Runs inside the existing offscreen decoder/export gate, on every backend.
bool TestHiddenVideo(const std::filesystem::path& root, const std::filesystem::path& builtins,
                     const rhi::BackendOptions& options) {
    std::string reason;
#define VERIFY(x)                                                                                  \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "hidden video " << __LINE__ << ": " << #x << ": " << reason << '\n';      \
            return false;                                                                          \
        }                                                                                          \
    } while (false)
    const auto path = root / "frozen.avi";
    std::filesystem::copy_file(root / "native.avi", path);
    editor::EditorSession session;
    VERIFY(session.ImportVideo(path, 160, 96, 160, 96, reason));
    VERIFY(session.Edit(
        "no floor",
        [](auto& d, auto&) {
            d.view.groundGrid = d.view.groundShadow = 0;
            d.appearance.floorVisible = 0;
            return true;
        },
        reason));
    VERIFY(session.ImportAccessory(root / "screen.x", reason));
    VERIFY(session.Edit(
        "capture background",
        [](auto& d, auto&) {
            d.view.captureMode = 3;
            d.accessories.front().state.position = {0, 10, 0};
            for (auto& key : d.accessories.front().keyRecords.dense)
                key.position = {0, 10, 0};
            return true;
        },
        reason));
    VERIFY(session.Seek(5, reason));
    media::VideoFrame decoded;
    {
        qt::VideoDecoder decoder;
        VERIFY(decoder.Open(path, reason));
        VERIFY(decoder.ReadScene(5. / 30, true, decoded, {}, reason));
    }
    VERIFY(Number(*decoded.image) == 5);
    const auto revision = session.Revision(), updates = session.Runtime().Counters().frames;
    const auto request = session.RequestVideo();
    VERIFY(session.PublishVideo(request, decoded, reason));
    VERIFY(session.Revision() == revision && session.Runtime().Counters().frames == updates);
    VERIFY(session.ShowVideo(false, reason) && session.Seek(20, reason));
    VERIFY(session.RequestVideo().seconds == 5. / 30);
    VERIFY(session.Assets().video.frozen.image == decoded.image);
    VERIFY(!session.PublishVideo({path, request.identity, 20. / 30, true}, decoded, reason));
    VERIFY(session.Save(root / "hidden-video.pmm", Write, reason));
    const auto snapshot = render::CaptureExport(session);
    const auto frozen = snapshot.assets.video.frozen.image;

    // A second viewport and replacement device must not need the source file
    // or depend on the first renderer's GPU upload/history.
    std::filesystem::rename(path, root / "frozen-away.avi");
    qt::Viewport viewport(session, builtins);
    viewport.SetOptions(options);
    VERIFY(Wait(viewport, reason));
    auto backend = rhi::CreateOffscreenBackendDevice(160, 96, options);
    auto replacement = rhi::CreateOffscreenBackendDevice(160, 96, options);
    VERIFY(backend && replacement);
    rhi::SceneRenderer renderer(*backend.device);
    std::vector<std::uint8_t> reference;
    for (unsigned n = 0; n < 3; ++n) {
        if (n == 1)
            VERIFY(renderer.Rebind(*replacement.device));
        VERIFY(Wait(viewport, reason, n == 2 ? 192 : 160));
        const auto& frame = viewport.PreparedFrame();
        bool consumer = false;
        for (const auto& pass : frame.passes)
            for (const auto& draw : pass.draws)
                consumer |= draw.sceneObject && draw.targetTextures[0].target == 6;
        VERIFY(consumer);
        rhi::PreparedModelFrame gpu;
        rhi::CompletionToken token;
        VERIFY(renderer.Prepare(1, frame, gpu) && renderer.Submit(gpu, token));
        rhi::AttachmentReadback pixels;
        VERIFY(renderer.ReadTarget(1, 6, pixels));
        rhi::RgbaImage image{1024, 1024, std::move(pixels.bytes)};
        VERIFY(Number(image) == 5);
        if (n == 0) {
            reference.assign(image.pixels.begin(), image.pixels.end());
            VERIFY(QImage(image.pixels.data(), 1024, 1024, 4096, QImage::Format_RGBA8888)
                       .save(qt::Path(root / "hidden-capture.png")));
        } else
            VERIFY(std::equal(reference.begin(), reference.end(), image.pixels.begin()));
        VERIFY(renderer.Collect());
    }
    VERIFY(renderer.Unload(1) && renderer.Collect());
    VERIFY(renderer.Stats().residentResources == 0);
    qt::Viewport another(session, builtins);
    another.SetOptions(options);
    VERIFY(another.PrepareFrame(160, 96, reason)); // ready without a decoder
    QImage still;
    VERIFY(qt::RenderImage(session.Runtime(), session.Assets(), builtins, options, 160, 96, still,
                           reason));
    VERIFY(still.save(qt::Path(root / "hidden-still.png")));

    // Export uses a frozen lease captured before any further editor changes.
    render::ExportSettings spec;
    spec.width = 160;
    spec.height = 96;
    spec.first = 24;
    spec.last = 26;
    spec.alpha = render::ExportAlpha::Opaque;
    VERIFY(
        qt::ExportImages(snapshot, builtins, options, root / "hidden-sequence", spec, {}, reason));
    VERIFY(qt::ExportMovie(snapshot, builtins, options, root / "hidden.mp4", spec,
                           qt::VideoProfile::Mp4, false, {}, reason));
    for (unsigned n = 24; n <= 26; ++n) {
        const auto output =
            qt::Path(root / "hidden-sequence") + QString("/frame_%1.png").arg(n, 8, 10, QChar('0'));
        VERIFY(QImage(output) == QImage(qt::Path(root / "hidden-sequence/frame_00000024.png")));
    }

    // A cold reopen MUST decode at the saved cursor (20), not the old freeze
    // (5), not zero, and not the range start (24). Missing media fails visibly.
    VERIFY(session.Open(root / "hidden-video.pmm", reason));
    VERIFY(!session.Document().avi.enabled && !session.Assets().video.frozen.image);
    VERIFY(session.RequestVideo().seconds == 20. / 30);
    VERIFY(!session.PublishVideo(request, decoded, reason));
    VERIFY(!Wait(viewport, reason));
    VERIFY(reason != "background-video-pending");
    VERIFY(!qt::ExportImages(render::CaptureExport(session), builtins, options,
                             root / "hidden-missing", spec, {}, reason));
    VERIFY(!std::filesystem::exists(root / "hidden-missing"));
    std::filesystem::rename(root / "frozen-away.avi", path);
    VERIFY(viewport.ReloadTextures(reason) && Wait(viewport, reason));
    VERIFY(Number(*session.Assets().video.frozen.image) == 20);
    VERIFY(!session.Dirty());
    const auto cold = render::CaptureExport(session);
    VERIFY(qt::ExportImages(cold, builtins, options, root / "hidden-reopened", spec, {}, reason));
    VERIFY(QImage(qt::Path(root / "hidden-reopened/frame_00000024.png")) !=
           QImage(qt::Path(root / "hidden-sequence/frame_00000024.png")));
    // Exporting before the first preview decode initializes the same load time.
    VERIFY(session.Open(root / "hidden-video.pmm", reason));
    VERIFY(qt::ExportImages(render::CaptureExport(session), builtins, options,
                            root / "hidden-cold-export", spec, {}, reason));
    VERIFY(QImage(qt::Path(root / "hidden-cold-export/frame_00000024.png")) ==
           QImage(qt::Path(root / "hidden-reopened/frame_00000024.png")));

    // The ordinary imported scene effect must retain the initialized video
    // prepass ahead of ScriptExternal and both fullscreen postprocess passes.
    std::filesystem::copy(MDS_EDITOR_EFFECTS_DIR, root / "hidden-effects",
                          std::filesystem::copy_options::recursive);
    VERIFY(session.AssignEffect({true, 0, -1}, root / "hidden-effects/Scene.fx", reason));
    mikudancestudio::mme::ParameterValue exposure;
    exposure.words = {0x3f000000}; // IEEE-754 0.5
    VERIFY(session.SetEffectParameter({true, 0, -1}, "Exposure", exposure, reason));
    VERIFY(Wait(viewport, reason));
    VERIFY(qt::ExportImages(render::CaptureExport(session), builtins, options,
                            root / "hidden-mme-sequence", spec, {}, reason));
    VERIFY(qt::ExportMovie(render::CaptureExport(session), builtins, options,
                           root / "hidden-mme.mp4", spec, qt::VideoProfile::Mp4, false, {},
                           reason));
    VERIFY(QImage(qt::Path(root / "hidden-mme-sequence/frame_00000024.png")) !=
           QImage(qt::Path(root / "hidden-reopened/frame_00000024.png")));

    // Capture modes use separate histories and returning to mode 3 never
    // resamples the hidden media. Device options don't change its identity.
    VERIFY(Wait(viewport, reason));
    for (int mode : {1, 2, 0, 3}) {
        VERIFY(session.Edit(
            "mode",
            [=](auto& d, auto&) {
                d.view.captureMode = mode;
                return true;
            },
            reason));
        VERIFY(Wait(viewport, reason));
        VERIFY(Number(*session.Assets().video.frozen.image) == 20);
    }
    VERIFY(session.Seek(30, reason) && session.ShowVideo(true, reason));
    VERIFY(Wait(viewport, reason) && Number(*session.Assets().video.frozen.image) == 30);
    VERIFY(session.ShowVideo(false, reason) && session.Seek(10, reason));
    session.RefreshVideo(); // native still-image mode-3 refresh event
    VERIFY(Wait(viewport, reason) && Number(*session.Assets().video.frozen.image) == 10);
    VERIFY(session.Edit(
        "bone box",
        [](auto& d, auto&) {
            d.timeline.editMode = 1;
            d.timeline.startAtCurrent = 1;
            return true;
        },
        reason));
    VERIFY(session.Play(reason) && session.Advance(.2, reason));
    session.Pause();
    VERIFY(!session.Assets().video.frozen.image);
    VERIFY(Wait(viewport, reason) && Number(*session.Assets().video.frozen.image) == 16);
    const auto stale = session.RequestVideo();
    VERIFY(session.ImportVideo(path, 160, 96, 160, 96, reason));
    VERIFY(!session.PublishVideo(stale, decoded, reason)); // same path, new media generation
    VERIFY(session.New(reason));
    VERIFY(!session.PublishVideo(session.RequestVideo(), decoded, reason));
    VERIFY(viewport.PrepareFrame(160, 96, reason));
    for (const auto& pass : viewport.PreparedFrame().passes)
        VERIFY(pass.name != "background_video");
    VERIFY(snapshot.assets.video.frozen.image == frozen && Number(*frozen) == 5);
    std::cout << "hidden video session, cold reopen, capture, recovery and export passed\n";
    return true;
#undef VERIFY
}
