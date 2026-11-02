#include "export.hpp"
#include "mikudancestudio/rhi_screen_texture.hpp"
#include "editor_window.hpp"
#include "numeric_dialog.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QTimer>
#include "portable_material_history_fixture.hpp"
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
    CHECK(QGuiApplication::platformName() == "offscreen");
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
    options.backend = rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_BACKEND") == "d3d11")
        options.backend = rhi::Backend::D3D11;
    if (qEnvironmentVariable("MDS_GPU_BACKEND") == "d3d12")
        options.backend = rhi::Backend::D3D12;
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    {
        qt::EditorWindow sizing(assets, true);
        auto& editor = sizing.Session();
        CHECK(editor.ImportModel(root / "articulated.pmx", reason));
        CHECK(editor.SetBone(0, 0, {2, 3, 4}, {0, 0, 0, 1}, reason));
        sizing.RenderWindow()->SetOptions(options);
        sizing.centralWidget()->resize(1024, 700);
        sizing.Refresh();
        QSize limits;
        CHECK(sizing.RenderWindow()->OutputLimits(limits, reason));
        CHECK(limits.width() >= 2048 && limits.height() >= 2048);
        CHECK(limits.width() <= 4096 && limits.height() <= 4096);
        const auto* runtime = &editor.Runtime();
        const auto frames = runtime->Counters().frames;
        const auto command = [&](int id) -> QAction* {
            for (auto* action : sizing.findChildren<QAction*>())
                if (action->property("mmdCommandId").toInt() == id)
                    return action;
            return nullptr;
        };
        for (bool separate : {false, true}) {
            if (separate)
                command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
            for (const char* language : {"en", "ja", "zh_CN"}) {
                CHECK(sizing.SetLanguage(language));
                for (bool commit : {false, true}) {
                    const auto before = editor.Document().envelope.settings;
                    const auto revision = editor.Revision();
                    bool dialogVisited = false, topmost = false;
                    QTimer::singleShot(0, &sizing, [&] {
                        auto* dialog =
                            dynamic_cast<qt::ScreenSizeDialog*>(QApplication::activeModalWidget());
                        if (!dialog) {
                            QApplication::closeAllWindows();
                            return;
                        }
                        dialogVisited = true;
                        topmost = dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint);
                        dialog->Field(0)->setText(separate ? "161" : "97");
                        dialog->Field(1)->setText(separate ? "97" : "73");
                        if (commit)
                            dialog->accept();
                        else
                            dialog->reject();
                    });
                    command(MDS_CMD_SCREEN_SIZE)->trigger();
                    CHECK(dialogVisited && topmost == separate && sizing.LastError().isEmpty());
                    const auto& settings = editor.Document().envelope.settings;
                    if (!commit) {
                        CHECK(editor.Revision() == revision &&
                              settings.renderWidth == before.renderWidth &&
                              settings.renderHeight == before.renderHeight);
                        continue;
                    }
                    CHECK(settings.renderWidth == (separate ? 161 : 97) &&
                          settings.renderHeight == (separate ? 97 : 73));
                    const auto expected =
                        separate
                            ? ui::MmdSeparateViewport(sizing.SeparateWindow()->width(),
                                                      sizing.SeparateWindow()->height(),
                                                      settings.renderWidth, settings.renderHeight)
                            : ui::MmdViewport(sizing.centralWidget()->width(),
                                              sizing.centralWidget()->height(),
                                              settings.editPanelWidth, settings.renderWidth,
                                              settings.renderHeight);
                    CHECK(sizing.RenderWindow()->size() == QSize(expected.width, expected.height));
                    QImage first, repeated;
                    CHECK(qt::RenderImage(editor.Runtime(), editor.Assets(), assets, options,
                                          settings.renderWidth, settings.renderHeight, first,
                                          reason));
                    CHECK(qt::RenderImage(editor.Runtime(), editor.Assets(), assets, options,
                                          settings.renderWidth, settings.renderHeight, repeated,
                                          reason));
                    CHECK(first.size() == QSize(settings.renderWidth, settings.renderHeight) &&
                          first == repeated);
                    CHECK(&editor.Runtime() == runtime && runtime->Counters().frames == frames &&
                          editor.CanUndo());
                    CHECK(!sizing.isVisible() &&
                          (!sizing.SeparateWindow() || !sizing.SeparateWindow()->isVisible()));
                }
            }
        }
        CHECK(sizing.Save(root / "screen-size.pmm"));
        CHECK(editor.Undo(reason));
        CHECK(editor.Document().envelope.settings.renderWidth == 161);
        CHECK(editor.Document().models[0].bonePose[0].components[0] == 0);
        CHECK(sizing.Open(root / "screen-size.pmm"));
        CHECK(editor.Document().envelope.settings.renderWidth == 161 &&
              editor.Document().envelope.settings.renderHeight == 97);
    }
    {
        QImage picture(79, 43, QImage::Format_RGBA8888);
        for (int y = 0; y < picture.height(); ++y)
            for (int x = 0; x < picture.width(); ++x)
                picture.setPixelColor(
                    x, y,
                    QColor(x * 255 / 78, y * 255 / 42, (x + y) % 2 ? 224 : 32, x < 39 ? 96 : 255));
        CHECK(picture.save(qt::Path(root / "background.png")));
        editor::EditorSession background;
        CHECK(background.ImportPicture(root / "background.png", 128, 96, reason));
        CHECK(background.Edit(
            "plain-background",
            [](auto& d, auto&) {
                d.view.groundGrid = d.view.groundShadow = d.selfShadowMode = 0;
                return true;
            },
            reason));
        render::SceneRenderSources imageSources;
        imageSources.picture = background.Assets().picture;
        rhi::FileAssetSource files(assets);
        render::SceneFrameProducer images;
        CHECK(images.Load(background.Runtime(), imageSources, files, reason));
        render::SceneRenderSettings size;
        size.width = 128;
        size.height = 96;
        size.shadow.mode = 0;
        rhi::RenderFrame a, b;
        CHECK(images.Build(background.Runtime(), size, a, reason));
        CHECK(a.passes.front().draws.size() == 1);
        CHECK(!a.passes.front().draws[0].pipeline.raster.alphaBlend);
        auto gpu = rhi::CreateOffscreenBackendDevice(128, 96, options);
        CHECK(gpu);
        rhi::SceneRenderer renderer(*gpu.device);
        const auto read = [&](const rhi::RenderFrame& frame, rhi::AttachmentReadback& out) {
            rhi::PreparedModelFrame prepared;
            rhi::CompletionToken token;
            return renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token) &&
                   renderer.ReadTarget(1, frame.finalColor, out);
        };
        rhi::AttachmentReadback first, next, stable;
        CHECK(read(a, first));
        CHECK(images.Commit());
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(b.passes.front().draws[0].pipeline.raster.alphaBlend);
        CHECK(read(b, next) && first.bytes != next.bytes);
        CHECK(!images.Commit());
        CHECK(read(b, stable) && next.bytes == stable.bytes);
        CHECK(images.Load(background.Runtime(), imageSources, files, reason));
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(read(b, stable) && next.bytes == stable.bytes);
        images.ResetHistory();
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(read(b, stable) && first.bytes == stable.bytes);
        const auto placement = background.Document().picture;
        const auto image = background.Assets().picture;
        const auto oldTexture = b.passes.front().draws[0].textures[0];
        const auto beforeDragVertices = b.passes.front().draws[0].vertices;
        const auto updates = background.Runtime().Counters().frames;
        CHECK(background.DragViewTool(1, 0, 3, false, true, .12f, reason));
        CHECK(background.DragViewTool(2, 5, -7, false, true, .12f, reason));
        CHECK(background.Runtime().Counters().frames == updates &&
              background.Assets().picture == image);
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(b.passes.front().draws[0].textures[0].id == oldTexture.id);
        CHECK(b.passes.front().draws[0].textures[0].version == oldTexture.version);
        CHECK(b.passes.front().draws[0].vertices.id == beforeDragVertices.id);
        CHECK(b.passes.front().draws[0].vertices.version > beforeDragVertices.version);
        CHECK(read(b, next) && next.bytes != first.bytes);
        CHECK(read(b, stable) && stable.bytes == next.bytes);
        CHECK(background.Edit(
            "restore-picture",
            [&](auto& doc, auto&) {
                doc.picture = placement;
                return true;
            },
            reason));
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(read(b, stable) && stable.bytes == first.bytes);
        const auto oldVertices = b.passes.front().draws[0].vertices;
        size.width = 193;
        size.height = 111;
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(b.passes.front().draws[0].vertices.id == oldVertices.id);
        CHECK(b.passes.front().draws[0].vertices.version > oldVertices.version);
        CHECK(read(b, stable) && stable.bytes.size() == 193 * 111 * 4);
        CHECK(background.ShowPicture(false, reason));
        CHECK(images.Build(background.Runtime(), size, b, reason));
        CHECK(b.passes.front().draws.empty());
        CHECK(background.ShowPicture(true, reason));
        CHECK(qt::ExportSequence(background, assets, options, root / "background-full", 0, 2, 128,
                                 96, {}, reason));
        CHECK(qt::ExportSequence(background, assets, options, root / "background-late", 1, 2, 128,
                                 96, {}, reason));
        CHECK(QImage(qt::Path(root / "background-full/frame_00000001.png")) ==
              QImage(qt::Path(root / "background-late/frame_00000001.png")));
    }
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
    CHECK(session.New(reason) && session.ImportModel(root / "outline.pmx", reason));
    CHECK(session.Edit(
        "outline-view",
        [](auto& d, auto&) {
            d.view.groundGrid = d.view.groundShadow = d.selfShadowMode = 0;
            return true;
        },
        reason));
    CHECK(session.SelectModel(0, reason));
    sources.models = session.Assets().modelPaths;
    CHECK(producer.Load(session.Runtime(), sources, builtin, reason));
    settings.shadow.mode = 0;
    CHECK(producer.Build(session.Runtime(), settings, before, reason));
    QImage edgeFirst, edgeThick, edgeRestored;
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, edgeFirst,
                          reason));
    const auto edgeFrames = session.Runtime().Counters().frames;
    const auto edgeUpdates = session.Runtime().Counters().editorUpdates;
    CHECK(session.SetEdgeThickness(8, reason));
    CHECK(producer.Build(session.Runtime(), settings, after, reason));
    CHECK(before.passes[0].draws.front().vertices.version ==
          after.passes[0].draws.front().vertices.version);
    CHECK(before.passes[0].draws.back().vertices.version <
          after.passes[0].draws.back().vertices.version);
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, edgeThick,
                          reason));
    CHECK(edgeFirst != edgeThick);
    CHECK(session.SetEdgeThickness(1, reason));
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                          edgeRestored, reason));
    CHECK(edgeFirst == edgeRestored);
    CHECK(session.SetOutlineColor({17, 223, 91}));
    QImage coloredEdge, coloredRepeated;
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                          coloredEdge, reason));
    CHECK(coloredEdge == edgeFirst); // PMX retains its material's edge color.
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                          coloredRepeated, reason) &&
          coloredRepeated == coloredEdge);
    CHECK(session.SetOutlineColor({0, 0, 0}));
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                          edgeRestored, reason) &&
          edgeFirst == edgeRestored);
    CHECK(session.Runtime().Counters().frames == edgeFrames &&
          session.Runtime().Counters().editorUpdates == edgeUpdates);
    CHECK(session.New(reason) && session.ImportModel(root / "outline.pmd", reason));
    CHECK(session.SetEdgeThickness(8, reason));
    CHECK(session.SetSelfShadowEnabled(true));
    QImage fixedPmdEdge;
    for (int mode : {0, 1, 2}) {
        CHECK(session.Edit(
            "pmd-outline-mode",
            [&](auto& d, auto&) {
                d.view.groundGrid = d.view.groundShadow = 0;
                d.selfShadowMode = std::uint8_t(mode);
                return true;
            },
            reason));
        CHECK(session.SetOutlineColor({0, 0, 0}));
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                              edgeFirst, reason));
        if (!mode)
            fixedPmdEdge = edgeFirst;
        const auto shadowMode = session.Document().selfShadowMode;
        CHECK(session.SetSelfShadowEnabled(false));
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                              edgeRestored, reason) &&
              edgeRestored == fixedPmdEdge);
        CHECK(session.Document().selfShadowMode == shadowMode);
        CHECK(session.SetSelfShadowEnabled(true));
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                              edgeRestored, reason) &&
              edgeRestored == edgeFirst);
        CHECK(session.SetOutlineColor({17, 223, 91}));
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                              coloredEdge, reason) &&
              coloredEdge != edgeFirst);
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                              coloredRepeated, reason) &&
              coloredRepeated == coloredEdge);
        CHECK(session.SetOutlineColor({0, 0, 0}));
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                              edgeRestored, reason) &&
              edgeRestored == edgeFirst);
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
    CHECK(session.New(reason) && session.ImportAccessory(root / "bound-varied.x", reason));
    CHECK(session.Edit(
        "accessory-shadow",
        [](auto& d, auto&) {
            d.view.groundGrid = d.view.groundShadow = 0;
            d.selfShadowMode = 1;
            d.accessories[0].state.position = {1, 2, 3};
            d.accessories[0].state.rotation = {.1f, .2f, .3f};
            d.accessories[0].state.scale = .2f;
            return true;
        },
        reason));
    sources.models.clear();
    sources.accessories = session.Assets().accessoryPaths;
    CHECK(producer.Load(session.Runtime(), sources, builtin, reason));
    settings.shadow.mode = 1;
    CHECK(producer.Build(session.Runtime(), settings, before, reason));
    CHECK(before.passes[0].draws.size() == 2);
    CHECK(before.passes[1].draws.size() == 2);
    CHECK(before.passes[1].draws[0].pipeline.shadow.accessory);
    CHECK(before.passes[1].draws[0].pipeline.shadow.lightingWorld[12] == 0);
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, image,
                          reason));
    QImage accessoryRepeat;
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96,
                          accessoryRepeat, reason));
    CHECK(image == accessoryRepeat);
    CHECK(session.Edit(
        "accessory-additive",
        [](auto& d, auto&) {
            d.accessories[0].state.additiveBlend = 1;
            return true;
        },
        reason));
    CHECK(producer.Build(session.Runtime(), settings, after, reason));
    CHECK(after.passes[0].draws.empty());
    CHECK(after.passes[1].draws[0].pipeline.raster.destinationBlend == rhi::BlendFactor::One);
    CHECK(session.New(reason) && session.ImportAccessory(root / "screen.x", reason));
    CHECK(session.Edit(
        "screen-capture",
        [](auto& d, auto&) {
            d.view.groundGrid = d.view.groundShadow = 0;
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
    editor::EditorSession materialSession;
    CHECK(MaterialHistoryScene(materialSession, root, reason));
    CHECK(qt::ExportSequence(materialSession, assets, options, root / "history-full", 0, 2, 128, 96,
                             {}, reason));
    CHECK(qt::ExportSequence(materialSession, assets, options, root / "history-late", 1, 2, 128, 96,
                             {}, reason));
    const QImage materialFirst(qt::Path(root / "history-full/frame_00000000.png"));
    const QImage materialSecond(qt::Path(root / "history-full/frame_00000001.png"));
    const QImage materialLate(qt::Path(root / "history-late/frame_00000001.png"));
    CHECK(!materialFirst.isNull() && !materialSecond.isNull() && !materialLate.isNull());
    CHECK(materialFirst != materialSecond);
    CHECK(materialSecond == materialLate);
    std::cout
        << "Qt offscreen export: deterministic PNG, isolated scene, cancellation, save/reopen passed\n";
}
