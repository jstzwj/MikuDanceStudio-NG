#include "editor_window.hpp"
#include "toon_dialog.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/pmd_writer.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QTest>
#include <iostream>
#include <fstream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 4 && QGuiApplication::platformName() == "offscreen");
    const auto root = std::filesystem::absolute(argv[1]);
    QFile reference(QString::fromLocal8Bit(argv[3]));
    CHECK(reference.open(QIODevice::ReadOnly));
    const auto references = QJsonDocument::fromJson(reference.readAll()).object();
    editor::EditorSession seed;
    CHECK(seed.ImportModel(root / "triangle.pmd", reason));
    auto source = *seed.Runtime().Resource(0)->PmdSource();
    source.materials[0].toonReference = 0;
    source.englishBoneNames.resize(source.bones.size());
    source.englishMorphNames.resize(source.morphs.empty() ? 0 : source.morphs.size() - 1);
    source.englishGroupNames.resize(source.groupNames.size());
    std::vector<std::uint8_t> model;
    CHECK(format::WriteEnhancedPmd(source, model, reason));
    {
        std::ofstream file(root / "toon-render.pmd", std::ios::binary);
        file.write(reinterpret_cast<const char*>(model.data()), std::streamsize(model.size()));
        CHECK(file);
    }
    QImage privateToon(2, 2, QImage::Format_RGBA8888);
    privateToon.fill(Qt::cyan);
    CHECK(privateToon.save(qt::Path(root / "private.bmp")));
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(privateToon.save(qt::Path(root / "private.bmp")));
        qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
        CHECK(window.SetLanguage(language));
        auto& session = window.Session();
        const auto command = [&]() -> QAction* {
            for (auto* action : window.findChildren<QAction*>())
                if (action->property("mmdCommandId").toInt() == MDS_CMD_TOON_TEXTURES)
                    return action;
            return nullptr;
        };
        CHECK(command() && !command()->isEnabled());
        CHECK(session.ImportModel(root / "toon-render.pmd", reason));
        window.Refresh();
        CHECK(command()->isEnabled());
        qt::Language labels;
        CHECK(labels.Set(language));
        qt::ToonDialog geometry(session, labels);
        const auto expected =
            references[QString("toon.") + (std::string(language) == "en" ? "en" : "ja")].toObject();
        CHECK(geometry.Template().id == expected["id"].toInt());
        CHECK(geometry.Template().count == std::size_t(expected["controls"].toArray().size()));
        const int baseY = std::string(language) == "en" ? 15 : 12;
        const auto controls = expected["controls"].toArray();
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = geometry.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto r = controls[i].toObject()["rect"].toArray();
            CHECK(widget->geometry() ==
                  QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * baseY + 4) / 8,
                        (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * baseY + 4) / 8));
        }
        for (unsigned i = 0; i < 10; ++i)
            CHECK(geometry.Field(i) &&
                  geometry.Field(i)->text() == QString("toon%1.bmp").arg(i + 1, 2, 10, QChar('0')));
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        auto* view = window.RenderWindow();
        auto options = view->Options();
        const auto backendName = qEnvironmentVariable("MDS_GPU_BACKEND");
        if (backendName == "d3d11")
            options.backend = rhi::Backend::D3D11;
        else if (backendName == "d3d12")
            options.backend = rhi::Backend::D3D12;
        else if (backendName == "vulkan")
            options.backend = rhi::Backend::Vulkan;
        else if (backendName == "metal")
            options.backend = rhi::Backend::Metal;
        view->SetOptions(options);
        CHECK(view->PrepareFrame(127, 95, reason));
        const auto original = view->PreparedFrame();
        auto backend = rhi::CreateOffscreenBackendDevice(127, 95, options);
        CHECK(backend);
        rhi::OffscreenBackendDevice replacement;
        rhi::SceneRenderer renderer(*backend.device);
        const auto draw = [&](rhi::AttachmentReadback& image) {
            const auto& frame = view->PreparedFrame();
            rhi::PreparedModelFrame prepared;
            rhi::CompletionToken token;
            return renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token) &&
                   renderer.ReadTarget(1, frame.finalColor, image);
        };
        rhi::AttachmentReadback before, after, repeated;
        CHECK(draw(before));
        bool seen = false;
        QTimer::singleShot(0, [&] {
            auto* dialog =
                dynamic_cast<qt::ToonDialog*>(window.findChild<QDialog*>("mmd.toonTextures"));
            if (!dialog)
                return;
            seen = true;
            dialog->Field(0)->setText("private.bmp");
            dialog->accept();
        });
        command()->trigger();
        CHECK(seen && session.EnhancedModelDirty() && !window.isVisible());
        std::array<std::string, 10> names;
        CHECK(session.ModelToonNames(0, names, reason) && names[0] == "private.bmp");
        CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames);
        CHECK(window.RenderWindow()->PrepareFrame(127, 95, reason));
        CHECK(draw(after) && before.bytes != after.bytes && draw(repeated) &&
              repeated.bytes == after.bytes);
        for (const auto& old : original.resources)
            if (old.kind != rhi::ReplayResourceKind::Texture) {
                const auto& current = view->PreparedFrame().resources;
                const auto found = std::find_if(current.begin(), current.end(), [&](const auto& r) {
                    return r.key.id == old.key.id;
                });
                CHECK(found != current.end() && found->key.version == old.key.version &&
                      found->bytes == old.bytes);
            }
        std::filesystem::remove(root / "private.bmp");
        view->SetOptions(options); // Replacement device/producer must retain CPU image ownership.
        CHECK(view->PrepareFrame(129, 97, reason));
        CHECK(view->PrepareFrame(127, 95, reason));
        replacement = rhi::CreateOffscreenBackendDevice(127, 95, options);
        CHECK(replacement && renderer.Rebind(*replacement.device));
        CHECK(draw(repeated) && repeated.bytes == after.bytes);
        std::vector<unsigned> absent;
        CHECK(session.SetModelToonNames(0,
                                        {"private.bmp", "toon02.bmp", "toon03.bmp", "toon04.bmp",
                                         "toon05.bmp", "toon06.bmp", "toon07.bmp", "toon08.bmp",
                                         "toon09.bmp", "toon10.bmp"},
                                        absent, reason));
        qt::ToonDialog cancel(session, labels);
        cancel.Field(0)->setText("checker.bmp");
        cancel.reject();
        CHECK(session.ModelToonNames(0, names, reason) && names[0] == "private.bmp");
        qt::ToonDialog partial(session, labels);
        partial.Field(1)->setText("checker.bmp");
        partial.Field(2)->setText("missing.bmp");
        std::vector<QString> failed;
        partial.missing = [&](const auto& name) { failed.push_back(name); };
        partial.accept();
        CHECK(partial.result() == QDialog::Rejected &&
              failed == std::vector<QString>{"missing.bmp"});
        CHECK(session.ModelToonNames(0, names, reason) && names[0] == "private.bmp" &&
              names[1] == "checker.bmp");
        CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames);
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(!command()->isEnabled());
        session.Pause();
        CHECK(session.UnloadModel(0, reason));
        CHECK(session.ImportModel(root / "triangle.pmx", reason));
        window.Refresh();
        CHECK(!command()->isEnabled());
    }
    std::cout
        << "Three-language toon template, menu, partial/default behavior and lifecycle passed\n";
}
