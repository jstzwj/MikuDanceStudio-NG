#include "editor_window.hpp"
#include "effect_dialog.hpp"
#include "export.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFile>
#include <fstream>
#include <iostream>
#include <cstring>
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
mikudancestudio::mme::ParameterValue Value(std::initializer_list<float> words) {
    mikudancestudio::mme::ParameterValue v;
    v.columns = unsigned(words.size());
    for (float f : words) {
        std::uint32_t word;
        std::memcpy(&word, &f, 4);
        v.words.push_back(word);
    }
    return v;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3 && QGuiApplication::platformName() == "offscreen");
    const auto root = std::filesystem::absolute(argv[1]),
               assets = std::filesystem::absolute(argv[2]);
    std::filesystem::copy(MDS_EDITOR_EFFECTS_DIR, root / "effects",
                          std::filesystem::copy_options::recursive);
    rhi::BackendOptions options;
    const auto api = qEnvironmentVariable("MDS_GPU_BACKEND");
    options.backend = api == "d3d12"    ? rhi::Backend::D3D12
                      : api == "d3d11"  ? rhi::Backend::D3D11
                      : api == "vulkan" ? rhi::Backend::Vulkan
                      : api == "metal"  ? rhi::Backend::Metal
                                        : rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    qt::EditorWindow window(assets, true);
    auto& session = window.Session();
    window.RenderWindow()->SetOptions(options);
    CHECK(session.ImportModel(root / "articulated.pmx", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    auto placement = session.Document().accessories[0].state;
    placement.position[0] = 10000;
    CHECK(session.SetAccessory(0, placement, reason));
    CHECK(session.Register({{editor::Track::Accessory, 0, 0, 0}}, reason));
    CHECK(session.Edit(
        "simple-view",
        [](auto& d, auto&) {
            d.view.groundGrid = d.view.groundShadow = d.selfShadowMode = 0;
            return true;
        },
        reason));
    const editor::EffectAddress model{false, 0, -1}, carrier{true, 0, -1};
    CHECK(session.AssignEffect(model, root / "effects/Material.fx", reason));
    CHECK(session.SetEffectParameter(model, "Tint", Value({.2f, .6f, .8f, .5f}), reason));
    const auto render = [&](QImage& image, unsigned w = 127, unsigned h = 95) {
        return qt::RenderImage(session.Runtime(), session.Assets(), assets, options, w, h, image,
                               reason, session.DisplayOptions(), session.Effects());
    };
    const auto updates = session.Runtime().Counters().frames;
    QImage material, copy, repeated;
    CHECK(render(material));
    CHECK(!material.isNull());
    CHECK(session.AssignEffect(carrier, root / "effects/Scene.fx", reason));
    CHECK(render(copy));
    if (copy != material) {
        material.save("out/evidence/p55-material.png");
        copy.save("out/evidence/p55-copy.png");
    }
    CHECK(copy == material);
    CHECK(render(repeated) && copy == repeated);
    CHECK(session.Runtime().Counters().frames == updates);
    CHECK(session.SetEffectParameter(carrier, "Exposure", Value({.5f}), reason));
    CHECK(render(repeated));
    CHECK(repeated != copy);
    CHECK(session.UndoEffects(reason));
    CHECK(render(repeated) && repeated == copy);
    CHECK(session.RedoEffects(reason));
    CHECK(session.EnableEffect(carrier, false, reason));
    CHECK(render(repeated) && repeated == material);
    CHECK(session.EnableEffect(carrier, true, reason));
    CHECK(session.SetEffectParameter(carrier, "Exposure", Value({1}), reason));
    // Qt parameter editing uses the same logical assignment. Language/backend
    // changes keep it alive without evaluating CPU animation a second time.
    CHECK(window.RenderWindow()->PrepareFrame(127, 95, reason));
    auto* action = window.findChild<QAction*>("mme.effects");
    CHECK(action);
    action->trigger();
    auto* dialog = dynamic_cast<qt::EffectDialog*>(window.findChild<QDialog*>("mme.effects"));
    CHECK(dialog);
    auto* objects = dialog->findChild<QComboBox*>("mme.object");
    objects->setCurrentIndex(objects->findData(0));
    auto* table = dialog->findChild<QTableWidget*>("mme.parameters");
    CHECK(table && table->rowCount() >= 2);
    int row = -1;
    for (int n = 0; n < table->rowCount(); ++n)
        if (table->item(n, 0)->text() == "Tint")
            row = n;
    CHECK(row >= 0);
    table->item(row, 2)->setText("0.8 0.2 0.4 0.75");
    CHECK(session.Effects().assignments.at(model).parameters.at("Tint") ==
          Value({.8f, .2f, .4f, .75f}));
    const auto effectRevision = session.EffectRevision();
    const auto beforeLanguage = session.Runtime().Counters().frames;
    for (const auto& language : qt::Language::Available()) {
        CHECK(window.SetLanguage(language.code));
        CHECK(session.EffectRevision() == effectRevision);
        CHECK(session.Runtime().Counters().frames == beforeLanguage);
    }
    dialog->hide();
    CHECK(!window.isVisible());
    CHECK(window.Save(root / "mme.pmm"));
    CHECK(window.Open(root / "mme.pmm"));
    CHECK(session.Effects().assignments.at(model).parameters.at("Tint") ==
          Value({.8f, .2f, .4f, .75f}));
    CHECK(render(copy));
    CHECK(
        qt::ExportSequence(session, assets, options, root / "sequence", 0, 2, 127, 95, {}, reason));
    CHECK(QImage(qt::Path(root / "sequence/frame_00000000.png"))
              .convertToFormat(QImage::Format_RGBA8888) == copy);
    const auto beforeExport = session.Runtime().Counters().frames;
    CHECK(!qt::ExportSequence(
        session, assets, options, root / "cancelled", 0, 2, 127, 95,
        [](auto, auto) { return false; }, reason));
    CHECK(!std::filesystem::exists(root / "cancelled"));
    CHECK(session.Runtime().Counters().frames == beforeExport);
    CHECK(session.Play(reason));
    CHECK(session.Advance(1.0 / 30, reason));
    session.Pause();
    CHECK(render(repeated));
    CHECK(session.Seek(0, reason));
    CHECK(render(repeated));
    for (const auto size : {QSize(93, 67), QSize(128, 96)}) {
        QImage scaled;
        CHECK(render(scaled, unsigned(size.width()), unsigned(size.height())));
        CHECK(scaled.size() == size);
    }
    // Device replacement/backend recreation takes the same frozen CPU state.
    const auto beforeDevice = session.Runtime().Counters().frames;
    window.RenderWindow()->SetOptions(options);
    CHECK(window.RenderWindow()->PrepareFrame(127, 95, reason));
    CHECK(session.Runtime().Counters().frames == beforeDevice);
#ifdef _WIN32
    for (auto backend : {rhi::Backend::D3D11, rhi::Backend::D3D12}) {
        for (auto* menuAction : window.findChildren<QAction*>())
            if (menuAction->property("rhiBackend").isValid() &&
                menuAction->property("rhiBackend").toInt() == int(backend))
                menuAction->trigger();
        CHECK(window.RenderWindow()->Options().backend == backend);
        CHECK(window.RenderWindow()->PrepareFrame(127, 95, reason));
        QImage switched;
        CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets,
                              window.RenderWindow()->Options(), 127, 95, switched, reason,
                              session.DisplayOptions(), session.Effects()));
        CHECK(!switched.isNull() && session.Runtime().Counters().frames == beforeDevice);
    }
    window.RenderWindow()->SetOptions(options);
#endif
    // Separate instances of the same FX must retain distinct parameter values.
    CHECK(session.ImportModel(root / "triangle.pmd", reason));
    const editor::EffectAddress second{false, 1, -1};
    CHECK(session.AssignEffect(second, root / "effects/Material.fx", reason));
    CHECK(session.SetEffectParameter(second, "Tint", Value({.1f, .9f, .3f, 1}), reason));
    CHECK(render(repeated));
    CHECK(session.UnloadModel(1, reason));
    CHECK(!session.Effects().assignments.count(second));
    // Failure is explicit and atomic. No unchanged image is returned as success.
    std::ofstream(root / "effects/broken.fx") << "unknown effect syntax";
    CHECK(session.AssignEffect(model, root / "effects/broken.fx", reason));
    QImage unchanged = copy;
    CHECK(!render(unchanged));
    CHECK(unchanged == copy);
    CHECK(!window.RenderWindow()->PrepareFrame(127, 95, reason));
    CHECK(!window.RenderWindow()->EffectReport().empty());
    session.SetBasicMaterials(true);
    CHECK(render(repeated));
    CHECK(repeated != copy);
    session.SetBasicMaterials(false);
    std::filesystem::copy_file(root / "effects/Material.fx", root / "effects/broken.fx",
                               std::filesystem::copy_options::overwrite_existing);
    CHECK(session.ReloadEffect(model, reason));
    CHECK(render(repeated));
    CHECK(session.AssignEffect(model, root / "effects/Material.fx", reason));
    CHECK(session.SetEffectParameter(model, "Tint", Value({.8f, .2f, .4f, .75f}), reason));
    CHECK(render(repeated) && repeated == copy);
    // Raw timeline semantics must follow the export runtime, not the editor's
    // saved PMM cursor. Each output frame has an independently known color.
    std::ifstream materialSource(root / "effects/Material.fx");
    const std::string source{std::istreambuf_iterator<char>(materialSource), {}};
    auto clockSource = source;
    clockSource.replace(clockSource.find("return Tint;"), 12,
                        "return float4(saturate(Raw),0,0,1);");
    std::ofstream(root / "effects/Clock.fx") << "float Raw:TIME2;\n" << clockSource;
    CHECK(session.AssignEffect(model, root / "effects/Clock.fx", reason));
    CHECK(qt::ExportSequence(session, assets, options, root / "clock-sequence", 0, 2, 127, 95, {},
                             reason));
    const auto hasColor = [](const QImage& image, int red) {
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto c = image.pixelColor(x, y);
                if (std::abs(c.red() - red) <= 1 && c.green() == 0 && c.blue() == 0 &&
                    c.alpha() == 255)
                    return true;
            }
        return false;
    };
    for (unsigned n = 0; n < 3; ++n)
        CHECK(hasColor(
            QImage(qt::Path(root / "clock-sequence" /
                            std::filesystem::path(
                                QString("frame_%1.png").arg(n, 8, 10, QChar('0')).toStdString()))),
            int(std::lround(n * 255. / 30))));
    auto controlSource = source;
    controlSource.replace(controlSource.find("return Tint;"), 12,
                          "return float4(saturate(Control.x/10),0,0,1);");
    std::ofstream(root / "effects/Control.fx")
        << "float3 Control:CONTROLOBJECT <string Name=\"(self)\";string Item=\"B\";>;\n"
        << controlSource;
    // Use a non-physical root: dynamic bodies deliberately override manual
    // bone edits in the articulated fixture used for playback above.
    CHECK(session.UnloadModel(0, reason));
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    CHECK(session.AssignEffect(model, root / "effects/Control.fx", reason));
    CHECK(session.SetBone(0, 0, {3, 0, 0}, {0, 0, 0, 1}, reason));
    const auto controlFrames = session.Runtime().Counters().frames;
    CHECK(render(repeated));
    CHECK(hasColor(repeated, 77));
    CHECK(session.Runtime().Counters().frames == controlFrames);
    controlSource = source;
    controlSource.replace(controlSource.find("return Tint;"), 12, "return float4(Control,0,0,1);");
    std::ofstream(root / "effects/AccessoryControl.fx")
        << "float Control:CONTROLOBJECT <string Name=\"bound.x\";string Item=\"Tr\";>;\n"
        << controlSource;
    placement.opacity = .4f;
    CHECK(session.SetAccessory(0, placement, reason));
    CHECK(session.AssignEffect(model, root / "effects/AccessoryControl.fx", reason));
    CHECK(render(repeated) && hasColor(repeated, 102));
    CHECK(!window.isVisible());
    std::cout
        << "P5.5 Qt MME: material, two-pass strict copy, parameters, 15 languages, save/reopen, playback/seek, resize/device, export/cancel, independent instances, explicit failure/reload passed\n";
}
