#include "export.hpp"
#include "mikudancestudio/rhi_screen_texture.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <QFile>
#include <iostream>
#include <cstring>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]),
               assets = std::filesystem::absolute(argv[2]);
    editor::EditorSession session;
    CHECK(session.ImportModel(root / "triangle.pmd", reason));
    CHECK(session.SetBone(0, 0, {0, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 0}}, reason));
    CHECK(session.SetBone(0, 0, {3, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 3}}, reason));
    const auto revision = session.Revision(), updates = session.Runtime().Counters().frames;
    rhi::BackendOptions options;
    options.backend = rhi::Backend::Vulkan;
    if (qEnvironmentVariable("MDS_GPU_BACKEND") == "d3d11")
        options.backend = rhi::Backend::D3D11;
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    CHECK(qt::ExportSequence(session, assets, options, root / "frames", 0, 3, 128, 96, {}, reason));
    CHECK(qt::ExportSequence(session, assets, options, root / "frames-repeat", 0, 3, 128, 96, {},
                             reason));
    for (unsigned i = 0; i < 4; ++i) {
        const auto name = QString("frame_%1.png").arg(i, 8, 10, QChar('0'));
        QFile a(qt::Path(root / "frames") + "/" + name),
            b(qt::Path(root / "frames-repeat") + "/" + name);
        CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        CHECK(a.readAll() == b.readAll());
    }
    CHECK(!qt::ExportSequence(
        session, assets, options, root / "cancelled", 0, 3, 128, 96,
        [](unsigned done, unsigned) { return done < 2; }, reason));
    CHECK(reason == "export-cancelled");
    CHECK(!std::filesystem::exists(root / "cancelled"));
    CHECK(session.Revision() == revision && session.Runtime().Counters().frames == updates);
    CHECK(session.Save(
        root / "render.pmm",
        [](const auto& p, const auto& bytes, auto&) {
            QFile f(qt::Path(p));
            return f.open(QIODevice::WriteOnly) &&
                   f.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) ==
                       qint64(bytes.size());
        },
        reason));
    CHECK(session.Open(root / "render.pmm", reason));
    QImage image;
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, image,
                          reason));
    CHECK(image.size() == QSize(128, 96));
    // Exercise C1 scene families using public models and the same production
    // producer as the viewport. A camera edit must only change edge bytes;
    // main deformation buffers keep their resource version.
    CHECK(session.Edit(
        "ground-shadow",
        [](auto& d, auto&) {
            d.view.groundGrid = 0;
            d.view.groundShadow = 1;
            d.physics.projectedShadowBlend = 1;
            d.physics.projectedShadowAmbient = .5f;
            d.selfShadowMode = 0;
            return true;
        },
        reason));
    render::SceneRenderSources sources;
    sources.models = session.Assets().modelPaths;
    rhi::FileAssetSource builtin(assets);
    render::SceneFrameProducer producer;
    CHECK(producer.Load(session.Runtime(), sources, builtin, reason));
    render::SceneRenderSettings settings;
    settings.width = 128;
    settings.height = 96;
    settings.shadow.mode = 0;
    rhi::RenderFrame before, after;
    const auto count = session.Runtime().Counters().frames;
    CHECK(producer.Build(session.Runtime(), settings, before, reason));
    CHECK(session.Runtime().Counters().frames == count);
    const auto& draws = before.passes.front().draws;
    CHECK(draws.size() >= 2);
    CHECK(draws.front().pipeline.vertexLayout == rhi::VertexLayout::PositionColor);
    CHECK(draws.front().pipeline.raster.cull == rhi::BatchCull::None);
    CHECK(draws.front().pipeline.stencilReference == 2);
    CHECK(draws[1].pipeline.vertexLayout == rhi::VertexLayout::PositionNormalUv);
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.distance *= 2;
    CHECK(session.SetCamera(camera, reason));
    CHECK(producer.Build(session.Runtime(), settings, after, reason));
    CHECK(before.passes[0].draws[1].vertices.id == after.passes[0].draws[1].vertices.id);
    CHECK(before.passes[0].draws[1].vertices.version == after.passes[0].draws[1].vertices.version);
    CHECK(before.passes[0].draws[0].vertices.version < after.passes[0].draws[0].vertices.version);
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, image,
                          reason));
    CHECK(session.New(reason) && session.ImportModel(root / "triangle.pmx", reason));
    sources.models = session.Assets().modelPaths;
    CHECK(producer.Load(session.Runtime(), sources, builtin, reason));
    for (int mode : {0, 1, 2}) {
        settings.shadow.mode = mode;
        settings.shadow.mapSize = 32;
        CHECK(producer.Build(session.Runtime(), settings, before, reason));
        const auto& main = before.passes[mode ? 1 : 0];
        CHECK(main.draws.back().pipeline.shadow.mode == rhi::BuiltinShadowMode::Outline);
        CHECK(main.draws.back().pipeline.raster.cull == rhi::BatchCull::Clockwise);
        CHECK(main.draws.back().pipeline.shadow.color[3] == 1);
        CHECK(rhi::ValidateRenderFrame(before));
        auto device = rhi::CreateOffscreenBackendDevice(128, 96, options);
        CHECK(device);
        rhi::SceneRenderer renderer(*device.device);
        rhi::PreparedModelFrame prepared;
        rhi::CompletionToken token;
        CHECK(renderer.Prepare(1, before, prepared));
        CHECK(renderer.Submit(prepared, token));
        rhi::AttachmentReadback first, second;
        CHECK(renderer.ReadTarget(1, before.finalColor, first));
        CHECK(renderer.Submit(prepared, token) &&
              renderer.ReadTarget(1, before.finalColor, second));
        CHECK(first.bytes == second.bytes);
    }
    CHECK(session.New(reason) && session.ImportModel(root / "tint-morph.pmx", reason));
    CHECK(session.Edit(
        "plain-morph-view",
        [](auto& d, auto&) {
            d.view.groundGrid = 0;
            d.selfShadowMode = 1;
            return true;
        },
        reason));
    QImage untinted, tinted, resetTint;
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, untinted,
                          reason));
    CHECK(session.SetMorph(0, 1, .5f, reason));
    CHECK(session.SetMorph(0, 2, .5f, reason));
    const auto& material = session.Runtime().Snapshot(0)->materials[0];
    CHECK(material.textureTint[0] == .75f && material.textureTint[3] == .875f);
    CHECK(material.textureAdd[0] == .1f && material.textureAdd[3] == .05f);
    CHECK(material.sphereTint[1] == .75f && material.sphereAdd[1] == .1f);
    CHECK(material.toonTint[0] == .75f && material.toonAdd[0] == .05f);
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, tinted,
                          reason));
    CHECK(untinted != tinted);
    // Texture morph alpha weights affect RGB blending; the output alpha still
    // comes from the original sampled texture and material, as in FX9.
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 128; ++x)
            CHECK(qAlpha(untinted.pixel(x, y)) == qAlpha(tinted.pixel(x, y)));
    CHECK(session.SetMorph(0, 1, 0, reason) && session.SetMorph(0, 2, 0, reason));
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, resetTint,
                          reason));
    CHECK(untinted == resetTint);
    CHECK(session.New(reason) && session.ImportAccessory(root / "screen.x", reason));
    CHECK(session.Edit(
        "screen-capture",
        [](auto& d, auto&) {
            d.view.groundGrid = 0;
            d.selfShadowMode = 0;
            d.view.captureMode = 1;
            return true;
        },
        reason));
    sources.models.clear();
    sources.accessories = session.Assets().accessoryPaths;
    CHECK(producer.Load(session.Runtime(), sources, builtin, reason));
    settings.shadow.mode = 0;
    CHECK(producer.Build(session.Runtime(), settings, before, reason));
    CHECK(!before.passes[0].draws[0].targetTextures[0].target);
    CHECK(producer.Build(session.Runtime(), settings, after, reason));
    CHECK(!after.passes[0].draws[0].targetTextures[0].target);
    auto historyDevice = rhi::CreateOffscreenBackendDevice(128, 96, options);
    CHECK(historyDevice);
    rhi::SceneRenderer historyRenderer(*historyDevice.device);
    rhi::PreparedModelFrame historyFrame;
    rhi::CompletionToken historyToken;
    CHECK(historyRenderer.Prepare(1, before, historyFrame) &&
          historyRenderer.Submit(historyFrame, historyToken));
    rhi::AttachmentReadback initialScreen, repeatedScreen, nestedScreen;
    CHECK(historyRenderer.ReadTarget(1, before.finalColor, initialScreen));
    CHECK(historyRenderer.Submit(historyFrame, historyToken));
    CHECK(historyRenderer.ReadTarget(1, before.finalColor, repeatedScreen));
    CHECK(initialScreen.bytes == repeatedScreen.bytes);
    producer.Commit();
    CHECK(producer.Build(session.Runtime(), settings, after, reason));
    CHECK(after.passes[0].draws[0].targetTextures[0].target ==
          rhi::ScreenTextureHistory::FirstTarget + 1);
    CHECK(historyRenderer.Prepare(1, after, historyFrame) &&
          historyRenderer.Submit(historyFrame, historyToken));
    CHECK(historyRenderer.ReadTarget(1, after.finalColor, nestedScreen));
    CHECK(initialScreen.bytes != nestedScreen.bytes);
    producer.Commit();
    producer.ResetHistory();
    CHECK(producer.Build(session.Runtime(), settings, after, reason));
    CHECK(!after.passes[0].draws[0].targetTextures[0].target);
    CHECK(historyRenderer.Prepare(1, after, historyFrame) &&
          historyRenderer.Submit(historyFrame, historyToken));
    CHECK(historyRenderer.ReadTarget(1, after.finalColor, repeatedScreen));
    CHECK(initialScreen.bytes == repeatedScreen.bytes);
    std::cout
        << "Qt offscreen export: deterministic PNG, isolated scene, cancellation, save/reopen passed\n";
}
