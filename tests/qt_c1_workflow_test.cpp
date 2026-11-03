#include "editor_window.hpp"
#include "export.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QImage>
#include <fstream>
#include <iostream>

using namespace mikudancestudio;
using editor::Track;
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
    CHECK(QGuiApplication::platformName() == "offscreen" && argc == 3);
    const auto root = std::filesystem::absolute(argv[1]),
               assets = std::filesystem::absolute(argv[2]);
    rhi::BackendOptions options;
    const auto api = qEnvironmentVariable("MDS_GPU_BACKEND");
    options.backend = api == "d3d11"    ? rhi::Backend::D3D11
                      : api == "d3d12"  ? rhi::Backend::D3D12
                      : api == "vulkan" ? rhi::Backend::Vulkan
                      : api == "metal"  ? rhi::Backend::Metal
                                        : rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    // File support and synchronization are exercised separately with silent
    // native devices. This combined workflow must never open an audio device.
    const auto wave = root / "workflow.wav";
    std::vector<unsigned char> wav(44 + 4800 * 2);
    const auto put = [&](unsigned n, unsigned value, unsigned size) {
        for (unsigned i = 0; i < size; ++i)
            wav[n + i] = std::uint8_t(value >> (8 * i));
    };
    std::memcpy(wav.data(), "RIFF", 4);
    put(4, unsigned(wav.size() - 8), 4);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put(16, 16, 4);
    put(20, 1, 2);
    put(22, 1, 2);
    put(24, 48000, 4);
    put(28, 96000, 4);
    put(32, 2, 2);
    put(34, 16, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    put(40, unsigned(wav.size() - 44), 4);
    std::ofstream output(wave, std::ios::binary);
    output.write(reinterpret_cast<const char*>(wav.data()), wav.size());
    output.close();
    qt::EditorWindow window(assets, true);
    window.RenderWindow()->SetOptions(options);
    auto& s = window.Session();
    CHECK(s.ImportModel(root / "morph-panels.pmd", reason));
    CHECK(s.ImportModel(root / "floor.pmx", reason));
    CHECK(s.ImportAccessory(root / "bound-varied.x", reason));
    CHECK(s.ImportAccessory(root / "screen.x", reason));
    CHECK(s.ImportPicture(root / "navigation-picture.tga", 127, 95, reason));
    CHECK(s.ImportWave(wave, reason));
    CHECK(s.Edit(
        "combined scene",
        [](auto& d, auto&) {
            d.physics.playbackMode = 0; // deterministic document editing phase
            d.physics.projectedShadowAmbient = .375f;
            d.physics.accessoryRenderOrder = 1;
            d.selfShadowMode = 1;
            d.view.groundGrid = d.view.groundShadow = 1;
            d.view.captureMode = 1;
            return true;
        },
        reason));
    s.SetPlaybackAudioDisabled(true);
    const auto click = [&](int id) {
        auto* button = static_cast<QAbstractButton*>(window.Control(id));
        if (!button->isEnabled()) {
            reason = "disabled control " + std::to_string(id);
            return false;
        }
        button->click();
        reason = window.LastError().toStdString();
        return reason.empty();
    };
    for (unsigned frame : {0u, 4u, 12u}) {
        CHECK(s.Seek(frame, reason));
        for (unsigned slot = 0; slot < 2; ++slot) {
            CHECK(s.SelectModel(std::uint8_t(slot), reason));
            CHECK(s.SelectBones({0}, 0, reason));
            CHECK(s.SetBone(std::uint8_t(slot), 0, {float(frame) * .025f, float(slot), 0},
                            {0, 0, 0, 1}, reason));
            CHECK(s.Register({{Track::Bone, std::uint8_t(slot), 0, frame},
                              {Track::Display, std::uint8_t(slot), 0, frame}},
                             reason));
            CHECK(s.SetMorph(std::uint8_t(slot), slot ? 0 : 1, float(frame) / 24, reason));
            CHECK(s.RegisterMorph(std::uint8_t(slot), slot ? 0 : 1, reason));
        }
        CHECK(s.SelectModel(255, reason));
        CHECK(s.Register({{Track::Camera, 255, 0, frame},
                          {Track::Light, 255, 0, frame},
                          {Track::Shadow, 255, 0, frame},
                          {Track::Gravity, 255, 0, frame},
                          {Track::Accessory, 0, 0, frame},
                          {Track::Accessory, 1, 0, frame}},
                         reason));
    }
    // The original has separate model and camera/global clipboards. Exercise
    // all eight pools in one continuing session, retaining the other mode's
    // buffer and foreign-model state through mode changes and bone Undo/Redo.
    for (bool cameraMode : {false, true}) {
        CHECK(s.SelectModel(cameraMode ? 255 : 0, reason));
        std::set<editor::KeyAddress> copied;
        for (auto key : s.Keys()) {
            const bool model = key.track == Track::Bone || key.track == Track::Morph ||
                               key.track == Track::Display;
            if (key.frame == 4 && (cameraMode ? !model : (model && key.slot == 0)))
                copied.insert(key);
        }
        CHECK(copied.size() == (cameraMode ? 6u : 3u));
        CHECK(s.SetSelectedKeys(copied, reason));
        window.Refresh();
        CHECK(click(panel::kCopyFramesButton));
        CHECK(s.Seek(20, reason));
        window.Refresh();
        CHECK(click(panel::kPasteButton));
        const auto pasted = s.Selected().keys;
        for (auto key : copied) {
            key.frame = 20;
            CHECK(pasted.count(key));
        }
        if (!cameraMode) {
            CHECK(click(panel::kUndoButton));
            CHECK(click(panel::kRedoButton));
        }
    }
    // A repeated camera paste overwrites existing keys; it adds no duplicates.
    const auto keyCount = s.Keys().size();
    CHECK(click(panel::kPasteButton));
    CHECK(s.Keys().size() == keyCount);
    CHECK(s.SelectModel(0, reason));
    CHECK(s.Seek(5, reason));
    CHECK(s.InsertFrameLine(Track::Bone, reason, true));
    CHECK(s.DeleteFrameLine(Track::Bone, reason, true));
    CHECK(s.SelectModel(255, reason));
    CHECK(s.InsertFrameLine(Track::Camera, reason, true));
    CHECK(s.DeleteFrameLine(Track::Camera, reason, true));
    CHECK(s.InsertFrameLine(Track::Light, reason, true));
    CHECK(s.DeleteFrameLine(Track::Light, reason, true));
    CHECK(s.SelectModel(0, reason));
    CHECK(s.InsertFrameLine(Track::Morph, reason, true));
    CHECK(s.DeleteFrameLine(Track::Morph, reason, true));
    for (const char* language : {"en", "ja", "zh_CN"}) {
        const auto revision = s.Revision(), frames = s.Runtime().Counters().frames;
        const auto selection = s.Selected().keys;
        CHECK(window.SetLanguage(language));
        CHECK(s.Revision() == revision && s.Runtime().Counters().frames == frames &&
              s.Selected().keys == selection);
    }
    CHECK(s.Edit(
        "start physical playback",
        [](auto& d, auto&) {
            d.physics.playbackMode = 1;
            return true;
        },
        reason));
    CHECK(s.Seek(0, reason));
    window.Refresh();
    const auto beforeBodies = s.Runtime().Bodies(1);
    CHECK(click(panel::kPlayButton) && s.Playing());
    for (unsigned i = 0; i < 8; ++i)
        window.Tick(1. / 60);
    CHECK(click(panel::kPlayButton) && !s.Playing());
    CHECK(s.Document().timeline.currentFrame > 0);
    CHECK(!beforeBodies.empty() && beforeBodies != s.Runtime().Bodies(1));
    CHECK(s.Seek(4, reason));
    const auto project = root / "combined.pmm";
    CHECK(window.Save(project));
    const auto keys = s.Keys();
    CHECK(!s.Dirty());
    CHECK(qt::ExportSequence(s, assets, options, root / "combined-before", 0, 2, 127, 95, {},
                             reason));
    CHECK(window.Open(project));
    CHECK(s.Keys() == keys && s.Document().models.size() == 2 &&
          s.Document().accessories.size() == 2);
    CHECK(s.Assets().picture && s.Assets().wave == wave && !s.Dirty());
    CHECK(
        qt::ExportSequence(s, assets, options, root / "combined-after", 0, 2, 127, 95, {}, reason));
    for (unsigned i = 0; i < 3; ++i) {
        const auto name = QString("frame_%1.png").arg(i, 8, 10, QChar('0'));
        const QImage before(qt::Path(root / "combined-before") + "/" + name),
            after(qt::Path(root / "combined-after") + "/" + name);
        CHECK(!before.isNull() && before == after);
    }
    CHECK(s.SelectModel(0, reason));
    CHECK(s.UnloadModel(0, reason) && s.UnloadAccessory(0, reason));
    CHECK(!s.CanUndo() && s.Document().models.size() == 1 && s.Document().accessories.size() == 1);
    CHECK(s.New(reason) && window.Open(project));
    CHECK(s.Document().models.size() == 2 && s.Document().accessories.size() == 2);
    CHECK(!window.isVisible() && window.RenderWindow()->Submissions() == 0);
    std::cout
        << "Combined C1 document/UI/clipboard/play/save/reopen/export workflow passed without native windows or audio\n";
}
