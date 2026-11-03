#include "editor_window.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QFile>
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
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3 && QGuiApplication::platformName() == "offscreen");
    const auto root = std::filesystem::absolute(argv[1]);
    QFile original(qt::Path(root / "checker.bmp"));
    CHECK(original.open(QIODevice::ReadOnly));
    const auto saved = original.readAll();
    original.close();
    for (const char* language : {"en", "ja", "zh_CN"})
        for (const char* model : {"triangle.pmd", "triangle.pmx"})
            for (int shadowMode : {0, 1, 2}) {
                qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
                CHECK(window.SetLanguage(language));
                auto& session = window.Session();
                CHECK(session.ImportModel(root / model, reason));
                CHECK(session.ImportAccessory(root / "bound.x", reason));
                CHECK(session.Edit(
                    "reload-fixture",
                    [&](auto& doc, auto&) {
                        doc.view.groundGrid = doc.view.groundShadow = 0;
                        doc.selfShadowMode = shadowMode;
                        return true;
                    },
                    reason));
                if (shadowMode == 2)
                    CHECK(session.Play(reason));
                auto* view = window.RenderWindow();
                auto options = view->Options();
                const auto requested = qEnvironmentVariable("MDS_GPU_BACKEND");
                if (requested == "d3d11")
                    options.backend = rhi::Backend::D3D11;
                else if (requested == "d3d12")
                    options.backend = rhi::Backend::D3D12;
                else if (requested == "vulkan")
                    options.backend = rhi::Backend::Vulkan;
                else if (requested == "metal")
                    options.backend = rhi::Backend::Metal;
                else
                    CHECK(requested.isEmpty());
                view->SetOptions(options);
                CHECK(view->PrepareFrame(127, 95, reason));
                const auto initial = view->PreparedFrame();
                auto backend = rhi::CreateOffscreenBackendDevice(127, 95, options);
                CHECK(backend);
                rhi::SceneRenderer renderer(*backend.device);
                const auto draw = [&](const rhi::RenderFrame& frame, rhi::AttachmentReadback& out) {
                    rhi::PreparedModelFrame prepared;
                    rhi::CompletionToken token;
                    return renderer.Prepare(1, frame, prepared) &&
                           renderer.Submit(prepared, token) &&
                           renderer.ReadTarget(1, frame.finalColor, out);
                };
                rhi::AttachmentReadback before, after, repeat;
                CHECK(draw(initial, before));
                QImage changed(8, 8, QImage::Format_RGB32);
                changed.fill(Qt::magenta);
                CHECK(changed.save(qt::Path(root / "checker.bmp")));
                // Disk edits alone do not change a resident frame.
                CHECK(view->PrepareFrame(127, 95, reason) && draw(view->PreparedFrame(), after));
                CHECK(before.bytes == after.bytes);
                QAction* reload = nullptr;
                for (auto* action : window.findChildren<QAction*>())
                    if (action->property("mmdCommandId").toInt() == MDS_CMD_RESTORE_TEXTURES)
                        reload = action;
                CHECK(reload && reload->isEnabled() && reload->property("mmdImplemented").toBool());
                const auto revision = session.Revision(),
                           updates = session.Runtime().Counters().frames;
                const auto* runtime = &session.Runtime();
                const auto undo = session.CanUndo(), redo = session.CanRedo();
                reload->trigger();
                CHECK(window.LastError().isEmpty() && session.Revision() == revision &&
                      session.Runtime().Counters().frames == updates &&
                      &session.Runtime() == runtime && session.CanUndo() == undo &&
                      session.CanRedo() == redo);
                CHECK(view->PrepareFrame(127, 95, reason));
                const auto updated = view->PreparedFrame();
                CHECK(draw(updated, after) && after.bytes != before.bytes);
                if (updated.resources.size() != initial.resources.size()) {
                    std::cerr << "reload profile " << language << '/' << model << '/' << shadowMode
                              << " resources=" << initial.resources.size() << '/'
                              << updated.resources.size() << '\n';
                    for (const auto& resource : initial.resources)
                        std::cerr << " old " << resource.key.id << ':' << resource.key.version
                                  << " kind=" << int(resource.kind) << '\n';
                    for (const auto& resource : updated.resources)
                        std::cerr << " new " << resource.key.id << ':' << resource.key.version
                                  << " kind=" << int(resource.kind) << '\n';
                }
                CHECK(updated.resources.size() == initial.resources.size());
                unsigned textureChanges = 0;
                for (const auto& old : initial.resources) {
                    const auto found =
                        std::find_if(updated.resources.begin(), updated.resources.end(),
                                     [&](const auto& r) { return r.key.id == old.key.id; });
                    CHECK(found != updated.resources.end());
                    if (old.kind != rhi::ReplayResourceKind::Texture)
                        CHECK(found->key.version == old.key.version && found->bytes == old.bytes);
                    else {
                        CHECK(found->key.version >= old.key.version);
                        textureChanges += found->key.version > old.key.version;
                    }
                }
                CHECK(textureChanges == 2); // Same disk texture in model/accessory owners.
                reload->trigger();
                CHECK(view->PrepareFrame(127, 95, reason) && draw(view->PreparedFrame(), repeat));
                CHECK(repeat.bytes == after.bytes);
                for (const auto& old : updated.resources) {
                    const auto& resources = view->PreparedFrame().resources;
                    const auto found =
                        std::find_if(resources.begin(), resources.end(),
                                     [&](const auto& r) { return r.key.id == old.key.id; });
                    CHECK(found != resources.end() && found->key.version == old.key.version);
                }
                // Native reload clears missing cache entries, retaining their paths.
                CHECK(QFile::remove(qt::Path(root / "checker.bmp")));
                CHECK(view->ReloadTextures(reason));
                CHECK(view->PrepareFrame(127, 95, reason) && draw(view->PreparedFrame(), repeat));
                CHECK(repeat.bytes != after.bytes && repeat.bytes != before.bytes);
                CHECK(original.open(QIODevice::WriteOnly) && original.write(saved) == saved.size());
                original.close();
                CHECK(view->ReloadTextures(reason) && view->PrepareFrame(127, 95, reason));
                CHECK(draw(view->PreparedFrame(), repeat) && repeat.bytes == before.bytes);
                CHECK(session.Revision() == revision &&
                      session.Runtime().Counters().frames == updates);
                CHECK(!window.isVisible() && view->Submissions() == 0);
            }
    {
        editor::EditorSession session;
        CHECK(session.ImportModel(root / "triangle.pmx", reason));
        CHECK(session.Edit(
            "toon-reload-fixture",
            [](auto& doc, auto&) {
                doc.selfShadowMode = 1;
                doc.view.groundGrid = doc.view.groundShadow = 0;
                return true;
            },
            reason));
        auto sources = qt::EditorRenderSources(session.Assets());
        CHECK(sources.sharedToons ==
              std::filesystem::u8path(
                  (QCoreApplication::applicationDirPath() + "/data").toUtf8().toStdString()));
        sources.sharedToons = root / "toons";
        CHECK(std::filesystem::create_directory(sources.sharedToons));
        const auto toonPath = sources.sharedToons / "toon01.bmp";
        QImage toon(2, 2, QImage::Format_RGB32);
        toon.fill(QColor(32, 64, 96));
        CHECK(toon.save(qt::Path(toonPath)));
        rhi::FileAssetSource builtins(std::filesystem::absolute(argv[2]));
        render::SceneFrameProducer producer;
        render::SceneRenderSettings settings;
        settings.width = 127;
        settings.height = 95;
        rhi::RenderFrame initial, updated;
        CHECK(producer.Load(session.Runtime(), sources, builtins, reason));
        CHECK(producer.Build(session.Runtime(), settings, initial, reason));
        const auto shadowColor = [](const auto& frame) {
            for (const auto& pass : frame.passes)
                for (const auto& draw : pass.draws)
                    if (draw.pipeline.shadow.mode == rhi::BuiltinShadowMode::Material)
                        return draw.pipeline.shadow.toonColor;
            return std::array<float, 4>{};
        };
        CHECK(
            (shadowColor(initial) == std::array<float, 4>{32.f / 256, 64.f / 256, 96.f / 256, 1}));
        toon.fill(QColor(128, 160, 192));
        CHECK(toon.save(qt::Path(toonPath)));
        CHECK(producer.ReloadTextures(session.Runtime(), sources, builtins, reason));
        CHECK(producer.Build(session.Runtime(), settings, updated, reason));
        CHECK((shadowColor(updated) ==
               std::array<float, 4>{128.f / 256, 160.f / 256, 192.f / 256, 1}));
        unsigned changed = 0;
        for (const auto& resource : initial.resources) {
            const auto found =
                std::find_if(updated.resources.begin(), updated.resources.end(),
                             [&](const auto& r) { return r.key.id == resource.key.id; });
            CHECK(found != updated.resources.end());
            changed += found->key.version != resource.key.version;
            if (found->key.version != resource.key.version)
                CHECK(resource.kind == rhi::ReplayResourceKind::Texture);
        }
        CHECK(changed == 1);
        CHECK(QFile::remove(qt::Path(toonPath)));
        CHECK(producer.ReloadTextures(session.Runtime(), sources, builtins, reason));
        CHECK(producer.Build(session.Runtime(), settings, updated, reason));
        CHECK(shadowColor(updated) != shadowColor(initial));
    }
    std::puts(
        "Qt offscreen material reload: 18 language/model/shadow profiles, shared toon colors, exact pixels, stable geometry/history and missing-file recovery passed");
}
