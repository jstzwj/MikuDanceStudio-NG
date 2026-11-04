#include "editor_window.hpp"
#include "mikudancestudio/menu_commands.h"
#include "editor_physics_selection_fixture.hpp"
#include "editor_reduction_fixture.hpp"
#include "editor_morph_purge_fixture.hpp"
#include "editor_key_capacity_fixture.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMenuBar>
#include <QPointer>
#include <QMessageBox>
#include <QTimer>
#include <QStyleFactory>
#include <QThread>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
using namespace mikudancestudio;
using namespace editor;
std::string reason;
bool SameKeys(const std::set<KeyAddress>& a, const std::set<KeyAddress>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const auto& x, const auto& y) { return !(x < y) && !(y < x); });
}
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " " << reason << '\n';               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    CHECK(argc == 4);
    QFile file(QString::fromLocal8Bit(argv[3]));
    CHECK(file.open(QIODevice::ReadOnly));
    const auto reference = QJsonDocument::fromJson(file.readAll()).object();
    const auto root = std::filesystem::absolute(argv[1]);
    qt::FileDialogHistory history;
    const auto base = qt::Path(root);
    for (unsigned i = 0; i < unsigned(qt::FileKind::Count); ++i) {
        const auto kind = qt::FileKind(i);
        if (kind == qt::FileKind::Vsq) {
            CHECK(history.Initial(kind, base) == QDir(base).filePath("UserFile/Vsq"));
            history.Confirm(kind, QDir(base).filePath("ignored/voice.vsq"));
            CHECK(history.Initial(kind, base) == QDir(base).filePath("UserFile/Vsq"));
            continue;
        }
        const auto path = QDir(base).filePath(QString("folder-%1/file.dat").arg(i));
        const auto initial = history.Initial(kind, base);
        history.Confirm(kind, {});
        CHECK(history.Initial(kind, base) == initial);
        history.Confirm(kind, path);
        CHECK(history.Initial(kind, base) == QFileInfo(path).absolutePath());
        history.enabled = false;
        history.Confirm(kind, QDir(base).filePath("ignored/file.dat"));
        CHECK(history.Initial(kind, base) == initial);
        history.enabled = true;
        CHECK(history.Initial(kind, base) == QFileInfo(path).absolutePath());
    }
    qt::FileDialogHistory restored;
    restored.Restore(history.Save());
    CHECK(restored.Save() == history.Save());
    restored.Restore({"malformed"});
    CHECK(restored.Save() == history.Save());
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    auto& session = window.Session();
    auto* autoCurve = static_cast<QAbstractButton*>(window.Control(panel::kPhysicsFrameCheckbox));
    CHECK(autoCurve->isChecked() && session.AutoInterpolation());
    const auto autoRevision = session.Revision();
    const auto* autoRuntime = &session.Runtime();
    autoCurve->click();
    CHECK(!session.AutoInterpolation() && !autoCurve->isChecked());
    CHECK(session.Revision() == autoRevision && &session.Runtime() == autoRuntime);
    window.SetLanguage("ja");
    CHECK(!session.AutoInterpolation() && !autoCurve->isChecked());
    window.SetLanguage("en");
    autoCurve->click();
    CHECK(session.AutoInterpolation() && session.Revision() == autoRevision);
    CHECK(session.Document().appearance.floorVisible == 1);
    const auto command = [&](int id) -> QAction* {
        for (auto* action : window.findChildren<QAction*>())
            if (action->property("mmdCommandId").toInt() == id)
                return action;
        return nullptr;
    };
    const auto tree = [&] {
        QJsonArray entries;
        const auto visit = [&](auto&& self, QAction* action, int depth) -> void {
            const char* kind = action->menu()          ? "Popup"
                               : action->isSeparator() ? "Separator"
                                                       : "Action";
            entries.append(QJsonObject{{"kind", kind},
                                       {"depth", depth},
                                       {"command", action->property("mmdCommandId").toInt()},
                                       {"text", action->text()}});
            if (action->menu())
                for (auto* child : action->menu()->actions())
                    self(self, child, depth + 1);
        };
        for (int i = 0; i < 8; ++i)
            visit(visit, window.menuBar()->actions().at(i), 0);
        return entries;
    };
    CHECK(window.menuBar()->actions().size() == 9);
    CHECK(window.menuBar()->actions().back()->menu()->objectName() == "menu.application");
    const auto actions = window.findChildren<QAction*>().size();
    const auto menus = window.findChildren<QMenu*>().size();
    for (int cycle = 0; cycle < 3; ++cycle)
        for (const char* language : {"en", "ja", "zh_CN"}) {
            QPointer<QAction> former = command(MDS_CMD_NEW_PROJECT);
            CHECK(former);
            const auto revision = session.Revision();
            const auto updates = session.Runtime().Counters().frames;
            CHECK(window.SetLanguage(language));
            CHECK(former && former == command(MDS_CMD_NEW_PROJECT));
            CHECK(window.findChildren<QAction*>().size() == actions);
            CHECK(window.findChildren<QMenu*>().size() == menus);
            CHECK(session.Revision() == revision && session.Runtime().Counters().frames == updates);
            if (reference.contains(language))
                CHECK(tree() == reference[language].toArray());
            for (int id = 200; id <= 302; ++id) {
                CHECK(command(id) && command(id)->menuRole() == QAction::NoRole);
                CHECK(!command(id)->text().isEmpty());
                CHECK(command(id)->text() != QString("command.%1").arg(id));
                if (!command(id)->property("mmdImplemented").toBool()) {
                    CHECK(!command(id)->isEnabled() && !command(id)->toolTip().isEmpty());
                    command(id)->trigger();
                    CHECK(session.Revision() == revision);
                }
            }
        }
    CHECK(window.SetLanguage("en"));
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(language));
        const auto revision = session.Revision(), appearance = session.AppearanceRevision();
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        auto* mipmaps = command(MDS_CMD_TOGGLE_MIPMAP);
        CHECK(mipmaps->isEnabled() && mipmaps->isChecked());
        mipmaps->trigger();
        CHECK(!mipmaps->isChecked() && !session.DisplayOptions().mipmaps);
        CHECK(session.AppearanceRevision() == appearance + 1);
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(command(MDS_CMD_TOGGLE_MIPMAP)->isEnabled());
        command(MDS_CMD_TOGGLE_MIPMAP)->trigger();
        CHECK(session.Playing() && session.DisplayOptions().mipmaps);
        session.Pause();
        CHECK(session.Revision() == revision && session.CanUndo() == undo &&
              session.CanRedo() == redo);
    }
    CHECK(window.SetLanguage("en"));
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(language));
        const auto revision = session.Revision(), appearance = session.AppearanceRevision();
        const auto updates = session.Runtime().Counters().frames;
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        auto* wireframe = command(MDS_CMD_TOGGLE_WIREFRAME);
        CHECK(wireframe->isEnabled() && !wireframe->isChecked());
        wireframe->trigger();
        CHECK(wireframe->isChecked() && session.DisplayOptions().wireframe);
        CHECK(session.AppearanceRevision() == appearance + 1 &&
              session.Runtime().Counters().frames == updates);
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(command(MDS_CMD_TOGGLE_WIREFRAME)->isEnabled());
        command(MDS_CMD_TOGGLE_WIREFRAME)->trigger();
        CHECK(session.Playing() && !session.DisplayOptions().wireframe);
        session.Pause();
        CHECK(session.Revision() == revision && session.CanUndo() == undo &&
              session.CanRedo() == redo);
    }
    CHECK(window.SetLanguage("en"));
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(language));
        const auto revision = session.Revision(), appearance = session.AppearanceRevision();
        const auto frames = session.Runtime().Counters().frames;
        auto* bodies = command(MDS_CMD_TOGGLE_PHYSICS_BODIES);
        CHECK(bodies->isEnabled() && !bodies->isChecked());
        bodies->trigger();
        CHECK(bodies->isChecked() && session.DisplayOptions().physicsBodies);
        CHECK(session.AppearanceRevision() == appearance + 1);
        CHECK(session.Play(reason));
        window.Refresh();
        const auto playingFrames = session.Runtime().Counters().frames;
        QKeyEvent key(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier);
        QApplication::sendEvent(window.RenderWindow(), &key);
        CHECK(session.Playing() && !session.DisplayOptions().physicsBodies);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier, "d", true);
        QApplication::sendEvent(window.RenderWindow(), &repeat);
        CHECK(!session.DisplayOptions().physicsBodies);
        session.Pause();
        CHECK(session.Revision() == revision &&
              session.Runtime().Counters().frames == playingFrames && playingFrames >= frames);
    }
    CHECK(window.SetLanguage("en"));
    auto aliasOptions = window.RenderWindow()->Options();
    const auto aliasBackend = qEnvironmentVariable("MDS_GPU_BACKEND");
    if (aliasBackend == "d3d11")
        aliasOptions.backend = rhi::Backend::D3D11;
    else if (aliasBackend == "d3d12")
        aliasOptions.backend = rhi::Backend::D3D12;
    else if (aliasBackend == "vulkan")
        aliasOptions.backend = rhi::Backend::Vulkan;
    else if (aliasBackend == "metal")
        aliasOptions.backend = rhi::Backend::Metal;
    auto unavailable = aliasOptions;
    unavailable.backend = rhi::Backend::Null;
    window.RenderWindow()->SetOptions(unavailable);
    window.Refresh();
    const auto unavailableRevision = session.AppearanceRevision();
    CHECK(!command(MDS_CMD_TOGGLE_ANTIALIASING)->isEnabled() &&
          !command(MDS_CMD_TOGGLE_ANTIALIASING)->isChecked());
    command(MDS_CMD_TOGGLE_ANTIALIASING)->trigger();
    QKeyEvent disabledAlias(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier);
    QApplication::sendEvent(window.RenderWindow(), &disabledAlias);
    CHECK(session.AppearanceRevision() == unavailableRevision);
    window.RenderWindow()->SetOptions(aliasOptions);
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(language));
        const auto revision = session.Revision(), appearance = session.AppearanceRevision();
        const auto updates = session.Runtime().Counters().frames;
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        auto* antialias = command(MDS_CMD_TOGGLE_ANTIALIASING);
        if (!antialias->isEnabled() || !antialias->isChecked())
            std::cerr << "antialias enabled=" << antialias->isEnabled()
                      << " checked=" << antialias->isChecked()
                      << " display=" << session.DisplayOptions().antialiasing
                      << " samples=" << window.RenderWindow()->AntialiasSamples()
                      << " language=" << language << '\n';
        CHECK(antialias->isEnabled() && antialias->isChecked());
        antialias->trigger();
        CHECK(!antialias->isChecked() && !session.DisplayOptions().antialiasing);
        CHECK(session.AppearanceRevision() == appearance + 1 &&
              session.Runtime().Counters().frames == updates);
        CHECK(session.Play(reason));
        window.Refresh();
        const auto playingFrames = session.Runtime().Counters().frames;
        QKeyEvent key(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier);
        QApplication::sendEvent(window.RenderWindow(), &key);
        CHECK(session.Playing() && session.DisplayOptions().antialiasing);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, "h", true);
        QApplication::sendEvent(window.RenderWindow(), &repeat);
        CHECK(session.DisplayOptions().antialiasing);
        session.Pause();
        CHECK(session.Revision() == revision &&
              session.Runtime().Counters().frames == playingFrames);
        CHECK(session.CanUndo() == undo && session.CanRedo() == redo);
    }
    CHECK(window.SetLanguage("en"));
    command(MDS_CMD_TOGGLE_UI_LANGUAGE)->trigger();
    CHECK(window.CurrentLanguage() == "ja");
    command(MDS_CMD_TOGGLE_UI_LANGUAGE)->trigger();
    CHECK(window.CurrentLanguage() == "en");

    // All selection commands preserve unrelated marks, including another model.
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    const std::vector<KeyAddress> added{
        {Track::Camera, 255, 0, 8},   {Track::Light, 255, 0, 9},    {Track::Shadow, 255, 0, 10},
        {Track::Gravity, 255, 0, 11}, {Track::Accessory, 0, 0, 12}, {Track::Accessory, 1, 0, 13},
        {Track::Bone, 0, 0, 14},      {Track::Bone, 1, 0, 15},      {Track::Display, 0, 0, 16}};
    CHECK(session.Register(added, reason));
    CHECK(session.SelectModel(0, reason));
    window.Refresh();
    const auto revision = session.Revision(), updates = session.Runtime().Counters().frames;
    const auto checkSelection = [&](int id, Track track, bool modelTrack) {
        auto before = session.Selected().keys;
        for (const auto& key : session.Keys())
            if (key.track == track && (!modelTrack || key.slot == session.Selected().model))
                before.insert(key);
        command(id)->trigger();
        return SameKeys(before, session.Selected().keys) && session.Revision() == revision &&
               session.Runtime().Counters().frames == updates;
    };
    session.Selected().keys = {{Track::Bone, 1, 0, 15}, {Track::Camera, 255, 0, 8}};
    CHECK(!command(MDS_CMD_SELECT_ALL_CAMERA_FRAMES)->isEnabled());
    CHECK(command(MDS_CMD_SELECT_ALL_BONE_FRAMES)->isEnabled());
    CHECK(checkSelection(MDS_CMD_SELECT_ALL_BONE_FRAMES, Track::Bone, true));
    CHECK(checkSelection(MDS_CMD_SELECT_ALL_MORPH_FRAMES, Track::Morph, true));
    CHECK(checkSelection(MDS_CMD_SELECT_ALL_DISPLAY_FRAMES, Track::Display, true));
    CHECK(session.SelectModel(255, reason));
    window.Refresh();
    // Mode switches can evaluate, but subsequent selection must not.
    const auto globalRevision = session.Revision(),
               globalUpdates = session.Runtime().Counters().frames;
    CHECK(!command(MDS_CMD_SELECT_ALL_BONE_FRAMES)->isEnabled());
    for (const auto& pair : {std::pair{MDS_CMD_SELECT_ALL_CAMERA_FRAMES, Track::Camera},
                             {MDS_CMD_SELECT_ALL_LIGHT_FRAMES, Track::Light},
                             {MDS_CMD_SELECT_ALL_SELF_SHADOW_FRAMES, Track::Shadow},
                             {MDS_CMD_SELECT_ALL_GRAVITY_FRAMES, Track::Gravity},
                             {MDS_CMD_SELECT_ALL_ACCESSORY_FRAMES, Track::Accessory}}) {
        auto expected = session.Selected().keys;
        for (const auto& key : session.Keys())
            if (key.track == pair.second)
                expected.insert(key);
        CHECK(command(pair.first)->isEnabled());
        command(pair.first)->trigger();
        CHECK(SameKeys(session.Selected().keys, expected));
        CHECK(session.Revision() == globalRevision &&
              session.Runtime().Counters().frames == globalUpdates);
    }
    for (auto pair : {std::pair{MDS_CMD_TOGGLE_INFORMATION, panel::kInfoCheckbox},
                      {MDS_CMD_TOGGLE_COORDINATE_AXIS, panel::kCoordAxisCheckbox},
                      {MDS_CMD_TOGGLE_CAMERA_TRACKING, panel::kFollowCameraCheckbox}}) {
        auto* control = static_cast<QAbstractButton*>(window.Control(pair.second));
        const auto before = control->isChecked();
        command(pair.first)->trigger();
        CHECK(control->isChecked() != before && command(pair.first)->isChecked() != before);
        control->click();
        CHECK(control->isChecked() == before && command(pair.first)->isChecked() == before);
    }
    for (int mode = 0; mode < 4; ++mode) {
        const int ids[]{MDS_CMD_PHYSICS_DISABLED, MDS_CMD_PHYSICS_ALWAYS, MDS_CMD_PHYSICS_ON_OFF,
                        MDS_CMD_PHYSICS_TRACE};
        command(ids[mode])->trigger();
        CHECK(session.Document().physics.playbackMode == mode);
        for (int i = 0; i < 4; ++i)
            CHECK(command(ids[i])->isChecked() == (i == mode));
    }
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(language));
        auto* policy = command(MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF);
        CHECK(policy->isEnabled() && !policy->isChecked());
        const auto revision = session.Revision(), updates = session.Runtime().Counters().frames;
        policy->trigger();
        CHECK(policy->isChecked() && session.PlaybackPhysicsOnOff());
        CHECK(session.Revision() == revision && session.Runtime().Counters().frames == updates);
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(policy->isEnabled() && session.Runtime().Simulation().physicsMode == 2);
        policy->trigger();
        CHECK(session.Playing() && !session.PlaybackPhysicsOnOff() && !policy->isChecked());
        CHECK(session.Runtime().Simulation().physicsMode == 2);
        session.Pause();
        CHECK(session.Runtime().Simulation().physicsMode == 3);
        CHECK(session.Document().physics.playbackMode == 3 && session.Revision() == revision);
    }
    for (int id : {MDS_CMD_TOGGLE_GROUND_SHADOW, MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY,
                   MDS_CMD_TOGGLE_BLACK_BACKGROUND}) {
        const auto before = command(id)->isChecked();
        command(id)->trigger();
        CHECK(command(id)->isChecked() != before);
        command(id)->trigger();
        CHECK(command(id)->isChecked() == before);
    }
    CHECK(session.UnloadModel(0, reason) && session.UnloadModel(1, reason));
    window.Refresh();
    CHECK(!command(MDS_CMD_LOAD_POSE)->isEnabled());
    CHECK(!command(MDS_CMD_SAVE_POSE)->isEnabled());
    CHECK(command(MDS_CMD_SELECT_ALL_CAMERA_FRAMES)->isEnabled());
    // The floor command updates the existing Bullet world and saved metadata;
    // it must not rebuild the session, stop playback or consume bone history.
    CHECK(session.ImportModel(root / "articulated.pmd", reason));
    CHECK(session.Play(reason));
    window.Refresh();
    const auto* runtime = &session.Runtime();
    const auto* snapshot = session.Runtime().Snapshot(0);
    const auto bodies = session.Runtime().Bodies(0);
    const auto floorUpdates = session.Runtime().Counters().frames;
    const auto floorRevision = session.Revision();
    const auto undo = session.CanUndo();
    command(MDS_CMD_TOGGLE_PHYSICS_FLOOR)->trigger();
    CHECK(session.Playing() && &session.Runtime() == runtime);
    CHECK(!session.Document().appearance.floorVisible &&
          !command(MDS_CMD_TOGGLE_PHYSICS_FLOOR)->isChecked());
    CHECK(session.Runtime().Snapshot(0) == snapshot && session.Runtime().Bodies(0) == bodies);
    CHECK(session.Runtime().Counters().frames == floorUpdates &&
          session.Revision() == floorRevision);
    CHECK(session.CanUndo() == undo);
    session.Pause();
    CHECK(window.Save(root / "menu-save.pmm"));
    CHECK(window.Open(root / "menu-save.pmm"));
    CHECK(!session.Document().appearance.floorVisible &&
          !command(MDS_CMD_TOGGLE_PHYSICS_FLOOR)->isChecked());
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.position[0] += 3;
    CHECK(session.SetCamera(camera, reason) && session.Dirty());
    const auto saveKey = QKeySequence::keyBindings(QKeySequence::Save).front()[0];
    QKeyEvent saveEvent(QEvent::KeyPress, saveKey.key(), saveKey.keyboardModifiers());
    QCoreApplication::sendEvent(window.Control(panel::kCurrentFrameEdit), &saveEvent);
    CHECK(!session.Dirty());
    CHECK(session.New(reason));
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    CHECK(session.Register({{Track::Bone, 0, 0, 5}, {Track::Morph, 0, 0, 5}}, reason));
    window.Refresh();
    command(MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE)->trigger();
    CHECK(session.Document().models[0].boneKeys.sparse[0].value.frame == 6);
    CHECK(session.Document().models[0].morphKeys.sparse[0].value.frame == 5);
    QKeyEvent insertMorph(QEvent::KeyPress, Qt::Key_U, Qt::NoModifier);
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &insertMorph);
    CHECK(session.Document().models[0].morphKeys.sparse[0].value.frame == 6);
    const auto inputRevision = session.Revision();
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_U, Qt::NoModifier, {}, true);
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &repeat);
    QKeyEvent typing(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, "i");
    QCoreApplication::sendEvent(window.Control(panel::kCurrentFrameEdit), &typing);
    CHECK(session.Revision() == inputRevision);
    CHECK(session.Play(reason));
    window.Refresh();
    CHECK(!command(MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE)->isEnabled());
    CHECK(!command(MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE)->isEnabled());
    CHECK(!command(MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE)->isEnabled());
    CHECK(!command(MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE)->isEnabled());
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &insertMorph);
    CHECK(session.Revision() == inputRevision && session.Playing());
    session.Pause();
    window.Refresh();
    command(MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE)->trigger();
    QKeyEvent deleteMorph(QEvent::KeyPress, Qt::Key_J, Qt::NoModifier);
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &deleteMorph);
    CHECK(session.Document().models[0].boneKeys.sparse[0].value.frame == 5);
    CHECK(session.Document().models[0].morphKeys.sparse[0].value.frame == 5);
    const auto deletionRevision = session.Revision();
    QKeyEvent deleteRepeat(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, {}, true);
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &deleteRepeat);
    QKeyEvent deleteTyping(QEvent::KeyPress, Qt::Key_J, Qt::NoModifier, "j");
    QCoreApplication::sendEvent(window.Control(panel::kCurrentFrameEdit), &deleteTyping);
    CHECK(session.Revision() == deletionRevision);
    CHECK(session.SelectModel(255, reason));
    CHECK(session.Register({{Track::Camera, 255, 0, 5}, {Track::Light, 255, 0, 5}}, reason));
    window.Refresh();
    command(MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE)->trigger();
    QKeyEvent insertCamera(QEvent::KeyPress, Qt::Key_I, Qt::ControlModifier);
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &insertCamera);
    CHECK(session.Document().cameraKeys.sparse[0].value.frame == 6);
    CHECK(session.Document().lightKeys.sparse[0].value.frame == 6);
    CHECK(session.Document().models[0].boneKeys.sparse[0].value.frame == 5);
    command(MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE)->trigger();
    QKeyEvent deleteCamera(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    QCoreApplication::sendEvent(window.Control(panel::kCopyFramesButton), &deleteCamera);
    CHECK(session.Document().cameraKeys.sparse[0].value.frame == 5);
    CHECK(session.Document().lightKeys.sparse[0].value.frame == 5);
    for (const char* language : {"en", "ja", "zh_CN"})
        for (const char* modelFile : {"articulated.pmd", "articulated.pmx", "triangle.pmd"}) {
            CHECK(session.New(reason) && session.ImportModel(root / modelFile, reason));
            CHECK(window.SetLanguage(language));
            const auto count = unsigned(session.Document().models[0].bonePose.size());
            const auto bone = count - 1;
            CHECK(session.SelectBones({0, bone}, int(bone), reason));
            CHECK(session.SetBone(0, bone, {2, 3, 4}, {0, .6f, 0, .8f}, reason));
            CHECK(window.Save(root / "menu-reset.pmm"));
            window.Refresh();
            const auto* runtime = &session.Runtime();
            const auto frames = runtime->Counters().frames;
            command(MDS_CMD_RESET_BONE_CAMERA_ROTATION)->trigger();
            CHECK(session.Dirty() && session.CanUndo());
            const auto& pose = session.Document().models[0].bonePose[bone];
            CHECK(pose.components[0] == 2 && pose.components[1] == 3 && pose.components[2] == 4);
            CHECK(pose.components[3] == 0 && pose.components[4] == 0 && pose.components[5] == 0 &&
                  pose.components[6] == 1 && pose.flags[1]);
            CHECK(session.Undo(reason));
            CHECK(session.Document().models[0].bonePose[bone].components[4] == .6f);
            CHECK(session.Undo(reason) && !session.CanUndo());
            const auto revision = session.Revision();
            const auto active = session.ActiveBone();
            window.Refresh();
            command(MDS_CMD_SELECT_PHYSICS_BONES)->trigger();
            CHECK(session.Revision() == revision && session.CanRedo());
            CHECK(session.Selected().bones ==
                  (count == 1 ? std::vector<std::uint32_t>{} : std::vector<std::uint32_t>{0}));
            CHECK(session.ActiveBone() == (count == 1 ? active : 0));
            command(MDS_CMD_RESET_PHYSICS_BODIES)->trigger();
            CHECK(session.Runtime().Simulation().resetPending);
            CHECK(session.Revision() == revision && session.CanRedo());
            CHECK(runtime == &session.Runtime() && runtime->Counters().frames == frames);
            CHECK(session.SelectBones({}, -1, reason));
            const auto emptyRevision = session.Revision();
            window.Refresh();
            command(MDS_CMD_RESET_BONE_CAMERA_ROTATION)->trigger();
            CHECK(session.Revision() == emptyRevision);
            CHECK(session.SelectModel(255, reason));
            auto camera = session.Runtime().Session()->Pose().camera;
            camera.position = {1, 2, 3};
            camera.rotation = {.2f, -.4f, .6f};
            camera.distance = -17;
            CHECK(session.SetCamera(camera, reason));
            CHECK(window.Save(root / "menu-reset.pmm"));
            window.Refresh();
            command(MDS_CMD_RESET_BONE_CAMERA_ROTATION)->trigger();
            const auto actual = session.Runtime().Session()->Pose().camera;
            CHECK(actual.position == camera.position && actual.distance == camera.distance &&
                  actual.rotation == (std::array<float, 3>{}));
            CHECK(session.Dirty() && !session.CanUndo());
            CHECK(window.Save(root / "menu-reset.pmm") && !session.Dirty());
            command(MDS_CMD_RESET_BONE_CAMERA_ROTATION)->trigger();
            CHECK(session.Dirty()); // Repeating zero rotation still marks the native document.
            CHECK(session.Play(reason));
            window.Refresh();
            CHECK(!command(MDS_CMD_RESET_BONE_CAMERA_ROTATION)->isEnabled());
            CHECK(command(MDS_CMD_RESET_PHYSICS_BODIES)->isEnabled());
            command(MDS_CMD_RESET_PHYSICS_BODIES)->trigger();
            CHECK(session.Playing() && session.Runtime().Simulation().resetPending);
            session.Pause();
        }
    unsigned implemented = 0;
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(PhysicsSelectionFixture(session, root, "articulated.pmx", true, 1, reason));
        CHECK(window.SetLanguage(language));
        CHECK(window.Save(root / "physics-selection.pmm"));
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        const auto count = unsigned(session.Document().models[0].bonePose.size());
        CHECK(command(MDS_CMD_SELECT_PHYSICS_ON_FRAMES)->isEnabled());
        command(MDS_CMD_SELECT_PHYSICS_ON_FRAMES)->trigger();
        CHECK(!session.Dirty() && session.Revision() == revision &&
              session.Runtime().Counters().frames == frames);
        CHECK(session.Selected().keys.count({Track::Bone, 0, count - 1, 0}) &&
              session.Selected().keys.count({Track::Bone, 0, count - 1, 5}));
        CHECK(!session.Selected().keys.count({Track::Bone, 0, count - 1, 2}));
        CHECK(window.Save(root / "physics-selection.pmm"));
        CHECK(window.Open(root / "physics-selection.pmm"));
        CHECK(session.Document().models[0].boneKeys.dense.back().allocated &&
              !session.Document().models[0].boneKeys.dense.front().allocated);
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_SELECT_PHYSICS_ON_FRAMES)->isEnabled());
        session.Pause();
        CHECK(session.SelectModel(255, reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_SELECT_PHYSICS_ON_FRAMES)->isEnabled());
    }
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(ReductionFixture(session, root, "articulated.pmx", true, 7, reason));
        CHECK(window.SetLanguage(language));
        CHECK(command(MDS_CMD_DELETE_UNUSED_FRAMES)->isEnabled());
        const auto before = session.Document().models[0].boneKeys.sparse.size();
        command(MDS_CMD_DELETE_UNUSED_FRAMES)->trigger();
        CHECK(session.Document().models[0].boneKeys.sparse.size() < before && session.CanUndo());
        CHECK(window.LastError().isEmpty());
        CHECK(session.Undo(reason));
        CHECK(session.Document().models[0].boneKeys.sparse.size() == before);
        CHECK(session.Redo(reason));
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_DELETE_UNUSED_FRAMES)->isEnabled());
        session.Pause();
        CHECK(session.SelectModel(255, reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_DELETE_UNUSED_FRAMES)->isEnabled());
    }
    for (int id = 200; id <= 302; ++id)
        implemented += command(id)->property("mmdImplemented").toBool();
    CHECK(implemented == 94);
    for (const char* language : {"en", "ja", "zh_CN"}) {
        qt::EditorWindow about(std::filesystem::absolute(argv[2]), true);
        CHECK(about.SetLanguage(language));
        for (bool separate : {false, true}) {
            const auto action = [&](int id) {
                for (auto* a : about.findChildren<QAction*>())
                    if (a->property("mmdCommandId").toInt() == id)
                        return a;
                return static_cast<QAction*>(nullptr);
            };
            if (separate)
                action(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
            for (bool playing : {false, true}) {
                if (playing)
                    CHECK(about.Session().Play(reason));
                about.Refresh();
                for (int id : {MDS_CMD_ABOUT, MDS_CMD_ABOUT_PHYSICS}) {
                    auto* frameTimer = about.findChild<QChronoTimer*>("editor.frameTimer");
                    CHECK(frameTimer);
                    frameTimer->start();
                    const auto revision = about.Session().Revision();
                    const auto updates = about.Session().Runtime().Counters().frames;
                    const auto appearance = about.Session().AppearanceRevision();
                    const bool physics = id == MDS_CMD_ABOUT_PHYSICS;
                    bool visited = false;
                    QTimer::singleShot(40, [&] {
                        auto* dialog =
                            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                        if (!dialog)
                            return;
                        const auto expectedTitle =
                            physics
                                ? about.CurrentLanguage() == "en" ? QString("about physical engine")
                                  : about.CurrentLanguage() == "ja"
                                      ? QString::fromUtf8(
                                            u8"\u7269\u7406\u30a8\u30f3\u30b8\u30f3\u306b\u3064\u3044\u3066")
                                      : QString::fromUtf8(u8"\u5173\u4e8e\u7269\u7406\u5f15\u64ce")
                                : QString("About");
                        visited =
                            dialog->icon() == QMessageBox::NoIcon &&
                            dialog->standardButtons() == QMessageBox::Ok &&
                            dialog->windowTitle() == expectedTitle &&
                            dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint) == physics &&
                            dialog->parentWidget() ==
                                (!physics && separate ? about.SeparateWindow() : &about) &&
                            dialog->textFormat() == Qt::PlainText;
                        visited &=
                            physics
                                ? dialog->text() ==
                                      "Bullet Physics Library Ver.2.75\n\nhttp://www.bulletphysics.com/"
                                : dialog->text().startsWith(
                                      "MikuDanceStudio-NG Ver." MDS_PRODUCT_VERSION "\n") &&
                                      dialog->text().contains("MikuMikuDance Ver.9.32");
                        if (!visited)
                            std::cerr << "about contract " << language << " separate=" << separate
                                      << " playing=" << playing << " command=" << id
                                      << " flags=" << int(dialog->windowFlags())
                                      << " parent=" << dialog->parentWidget() << " expected="
                                      << (!physics && separate ? about.SeparateWindow() : &about)
                                      << " title=" << dialog->windowTitle().toStdString()
                                      << " text=" << dialog->text().toStdString() << '\n';
                        dialog->done(QMessageBox::Ok);
                    });
                    CHECK(action(id)->isEnabled());
                    action(id)->trigger();
                    const auto resumed = frameTimer->isActive();
                    frameTimer->stop();
                    if (about.Session().Revision() != revision ||
                        about.Session().Runtime().Counters().frames != updates ||
                        about.Session().AppearanceRevision() != appearance ||
                        about.Session().Playing() != playing)
                        std::cerr << "about state " << language << " command=" << id
                                  << " revision=" << revision << '/' << about.Session().Revision()
                                  << " updates=" << updates << '/'
                                  << about.Session().Runtime().Counters().frames
                                  << " appearance=" << appearance << '/'
                                  << about.Session().AppearanceRevision() << " playing=" << playing
                                  << '/' << about.Session().Playing() << '\n';
                    CHECK(visited && resumed && about.Session().Revision() == revision &&
                          about.Session().Runtime().Counters().frames == updates &&
                          about.Session().AppearanceRevision() == appearance &&
                          about.Session().Playing() == playing);
                }
                about.Session().Pause();
            }
        }
        CHECK(!about.isVisible() && !about.SeparateWindow()->isVisible());
    }
    for (const char* language : {"en", "ja", "zh_CN"})
        for (int id : {MDS_CMD_DELETE_MOUTH_FRAMES, MDS_CMD_DELETE_EYE_FRAMES,
                       MDS_CMD_DELETE_EYEBROW_FRAMES}) {
            CHECK(MorphPurgeFixture(session, root, "morph-panels.pmx", true, 2, reason));
            CHECK(window.SetLanguage(language));
            auto* action = command(id);
            CHECK(action->isEnabled());
            const auto before = session.Document().models[0].morphKeys.sparse.size();
            const auto undo = session.CanUndo(), redo = session.CanRedo();
            action->trigger();
            CHECK(window.LastError().isEmpty() && session.Dirty());
            CHECK(session.Document().models[0].morphKeys.sparse.size() == before - 2);
            CHECK(session.CanUndo() == undo && session.CanRedo() == redo);
            CHECK(session.Play(reason));
            window.Refresh();
            CHECK(!action->isEnabled());
            session.Pause();
            CHECK(session.SelectModel(255, reason));
            window.Refresh();
            CHECK(!action->isEnabled());
        }
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(session.New(reason) && session.ImportModel(root / "outline.pmd", reason));
        session.SetHiddenModels(false);
        CHECK(window.SetLanguage(language));
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        auto* transparency = command(MDS_CMD_TOGGLE_MODEL_TRANSPARENCY);
        auto* hidden = command(MDS_CMD_TOGGLE_MODEL_NON_DISPLAY);
        CHECK(transparency->isEnabled() && hidden->isEnabled());
        transparency->trigger();
        CHECK(transparency->isChecked() && session.DisplayOptions().Transparent(0));
        QKeyEvent toggle(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier);
        QCoreApplication::sendEvent(window.centralWidget(), &toggle);
        CHECK(!transparency->isChecked());
        QKeyEvent hide(QEvent::KeyPress, Qt::Key_V, Qt::ShiftModifier);
        QCoreApplication::sendEvent(window.centralWidget(), &hide);
        CHECK(hidden->isChecked() && session.DisplayOptions().hideModels);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_V, Qt::ShiftModifier, "V", true);
        QCoreApplication::sendEvent(window.centralWidget(), &repeat);
        CHECK(hidden->isChecked());
        QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCoreApplication::sendEvent(window.centralWidget(), &paste);
        CHECK(hidden->isChecked() && !transparency->isChecked());
        CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames &&
              session.CanUndo() == undo && session.CanRedo() == redo);
        CHECK(session.Play(reason));
        window.Refresh();
        // The original Ctrl+V paste gate falls through to display toggles in playback.
        QKeyEvent playingV(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QCoreApplication::sendEvent(window.centralWidget(), &playingV);
        CHECK(session.Playing() && transparency->isChecked());
        QCoreApplication::sendEvent(window.centralWidget(), &paste);
        CHECK(session.Playing() && !hidden->isChecked());
        session.Pause();
        CHECK(window.Save(root / "model-display.pmm"));
        CHECK(window.Open(root / "model-display.pmm"));
        CHECK(session.DisplayOptions().transparentModels &&
              !session.DisplayOptions().Transparent(0));
        CHECK(session.ImportModel(root / "outline.pmd", reason));
        CHECK(session.DisplayOptions().Transparent(1));
        session.SetHiddenModels(true);
        CHECK(session.New(reason));
        CHECK(!session.DisplayOptions().transparentModels && session.DisplayOptions().hideModels);
        session.SetHiddenModels(false);
    }
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(session.New(reason) && window.SetLanguage(language));
        CHECK(session.SetSelfShadowEnabled(true));
        window.Refresh();
        auto* shadow = command(MDS_CMD_TOGGLE_SELF_SHADOW);
        CHECK(shadow->isEnabled() && shadow->isChecked());
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        const auto mode = session.Document().selfShadowMode;
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        shadow->trigger();
        CHECK(!shadow->isChecked() && !session.Document().selfShadowEnabled &&
              session.Document().selfShadowMode == mode);
        CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames &&
              session.CanUndo() == undo && session.CanRedo() == redo);
        QKeyEvent toggle(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier);
        QCoreApplication::sendEvent(window.centralWidget(), &toggle);
        CHECK(shadow->isChecked());
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier, "p", true);
        QCoreApplication::sendEvent(window.centralWidget(), &repeat);
        CHECK(shadow->isChecked());
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(shadow->isEnabled());
        shadow->trigger();
        CHECK(session.Playing() && !shadow->isChecked());
        session.Pause();
        CHECK(window.Save(root / "shadow-switch.pmm"));
        CHECK(session.SetSelfShadowEnabled(true));
        CHECK(window.Open(root / "shadow-switch.pmm"));
        CHECK(!command(MDS_CMD_TOGGLE_SELF_SHADOW)->isChecked());
        CHECK(session.New(reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_TOGGLE_SELF_SHADOW)->isChecked());
    }
    CHECK(session.SetSelfShadowEnabled(true));
    CHECK(window.FrameLimit() == 60 && command(MDS_CMD_FPS_60)->isChecked());
    const auto timer = window.findChild<QChronoTimer*>("editor.frameTimer");
    CHECK(timer && !timer->isActive());
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(language));
        for (int id : {MDS_CMD_FPS_30, MDS_CMD_FPS_UNLIMITED, MDS_CMD_FPS_60}) {
            const auto revision = session.Revision();
            const auto frames = session.Runtime().Counters().frames;
            const auto undo = session.CanUndo(), redo = session.CanRedo();
            command(id)->trigger();
            const auto rate = id == MDS_CMD_FPS_30 ? 30 : id == MDS_CMD_FPS_60 ? 60 : 1000;
            CHECK(window.FrameLimit() == unsigned(rate) &&
                  timer->interval() == std::chrono::nanoseconds(1000000000 / rate));
            CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames &&
                  session.CanUndo() == undo && session.CanRedo() == redo);
            for (int other : {MDS_CMD_FPS_30, MDS_CMD_FPS_UNLIMITED, MDS_CMD_FPS_60})
                CHECK(command(other)->isEnabled() && command(other)->isChecked() == (other == id));
        }
    }
    // Run the actual automatic timer while remaining offscreen. Display caps
    // change its cadence, never the 30fps document timeline's real-time speed.
    CHECK(session.New(reason));
    window.Refresh();
    for (int id : {MDS_CMD_FPS_30, MDS_CMD_FPS_60, MDS_CMD_FPS_UNLIMITED}) {
        command(id)->trigger();
        window.Dispatch(panel::kPlayButton);
        CHECK(session.Playing());
        const auto begin = session.CursorSeconds();
        QElapsedTimer wall;
        wall.start();
        timer->start();
        while (wall.elapsed() < 220) {
            app.processEvents();
            QThread::msleep(1);
        }
        timer->stop();
        const auto advanced = session.CursorSeconds() - begin;
        CHECK(std::abs(advanced - wall.nsecsElapsed() / 1e9) < .05);
        CHECK(session.Playing() && !window.isVisible() && !window.RenderWindow()->Submissions());
        window.Dispatch(panel::kPlayButton);
    }
    command(MDS_CMD_FPS_30)->trigger();
    CHECK(window.Save(root / "frame-cap.pmm"));
    CHECK(session.New(reason));
    window.Refresh();
    CHECK(window.FrameLimit() == 60 && command(MDS_CMD_FPS_60)->isChecked());
    command(MDS_CMD_FPS_60)->trigger();
    CHECK(window.Open(root / "frame-cap.pmm"));
    CHECK(window.FrameLimit() == 30 && command(MDS_CMD_FPS_30)->isChecked());
    const auto capRevision = session.Revision();
    CHECK(!session.SetFrameLimit(0) && !session.SetFrameLimit(31) &&
          session.Revision() == capRevision && window.FrameLimit() == 30);
    for (const char* language : {"en", "ja", "zh_CN"}) {
        CHECK(session.New(reason) && session.ImportModel(root / "articulated.pmx", reason));
        CHECK(session.Copy({{Track::Bone, 0, 0, 0}}, reason));
        CHECK(session.SelectBones({1}, 1, reason) && session.Seek(17, reason));
        CHECK(window.SetLanguage(language));
        CHECK(command(MDS_CMD_PASTE_AT_DIFFERENT_FRAME)->isEnabled());
        command(MDS_CMD_PASTE_AT_DIFFERENT_FRAME)->trigger();
        CHECK(window.LastError().isEmpty() && session.CanUndo());
        const auto hasKey = [&](unsigned frame) {
            for (const auto& key : session.Keys())
                if (key.track == Track::Bone && key.channel == 1 && key.frame == frame)
                    return true;
            return false;
        };
        CHECK(hasKey(17));
        CHECK(session.Undo(reason) && !hasKey(17));
        CHECK(session.Seek(23, reason));
        QKeyEvent paste(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
        QCoreApplication::sendEvent(window.centralWidget(), &paste);
        CHECK(hasKey(23));
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_PASTE_AT_DIFFERENT_FRAME)->isEnabled());
        session.Pause();
        CHECK(session.Copy({}, reason));
        window.Refresh();
        CHECK(!command(MDS_CMD_PASTE_AT_DIFFERENT_FRAME)->isEnabled());
    }
    CHECK(window.LastError().isEmpty());
    {
        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        auto& editor = owner.Session();
        CHECK(editor.ImportModel(root / "articulated.pmd", reason));
        owner.Refresh();
        const auto lowPowerMenu = [&]() -> QAction* {
            for (auto* action : owner.findChildren<QAction*>())
                if (action->property("mmdCommandId").toInt() == MDS_CMD_TOGGLE_LOW_POWER)
                    return action;
            return nullptr;
        };
        auto* checkbox = static_cast<QAbstractButton*>(owner.Control(panel::kFrameVolumeCheckbox));
        CHECK(!owner.LowPower() && !checkbox->isChecked() && !lowPowerMenu()->isChecked());
        const auto frames = [&] { return editor.Runtime().Counters().frames; };
        auto initial = frames();
        for (int i = 0; i < 4; ++i)
            owner.Tick(.016);
        CHECK(frames() == initial + 4);
        for (const char* language : {"en", "ja", "zh_CN"}) {
            CHECK(owner.SetLanguage(language));
            const auto revision = editor.Revision();
            const auto dirty = editor.Dirty(), undo = editor.CanUndo(), redo = editor.CanRedo();
            initial = frames();
            lowPowerMenu()->trigger();
            CHECK(owner.LowPower() && checkbox->isChecked() && lowPowerMenu()->isChecked());
            CHECK(editor.Revision() == revision && frames() == initial && editor.Dirty() == dirty &&
                  editor.CanUndo() == undo && editor.CanRedo() == redo);
            owner.Tick(.016);
            owner.Tick(.016);
            CHECK(frames() == initial + 2);
            for (int i = 0; i < 30; ++i)
                owner.Tick(.016);
            CHECK(frames() == initial + 2);
            // Self-generated paints and timer events must leave the scene asleep.
            QPaintEvent paint(QRect(0, 0, 10, 10));
            QCoreApplication::sendEvent(owner.centralWidget(), &paint);
            QEvent update(QEvent::UpdateRequest);
            QCoreApplication::sendEvent(owner.centralWidget(), &update);
            owner.Tick(.016);
            CHECK(frames() == initial + 2);
            // Panel edits, timeline, interpolation and native viewport inputs wake it.
            for (QObject* target : {static_cast<QObject*>(owner.Control(panel::kGotoFrameEdit)),
                                    static_cast<QObject*>(owner.TimelineWidget()),
                                    static_cast<QObject*>(owner.InterpolationWidget()),
                                    static_cast<QObject*>(owner.RenderWindow())}) {
                const auto before = frames();
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &release);
                for (int i = 0; i < 5; ++i)
                    owner.Tick(.016);
                CHECK(frames() == before + 2);
            }
            QInputMethodEvent ime;
            QCoreApplication::sendEvent(owner.Control(panel::kGotoFrameEdit), &ime);
            initial = frames();
            owner.Tick(.016);
            owner.Tick(.016);
            owner.Tick(.016);
            CHECK(frames() == initial + 2);
            CHECK(editor.Play(reason));
            initial = frames();
            for (int i = 0; i < 4; ++i)
                owner.Tick(.016);
            CHECK(editor.Playing() && frames() == initial + 4 && owner.LowPower());
            editor.Pause();
            owner.Refresh();
            initial = frames();
            for (int i = 0; i < 5; ++i)
                owner.Tick(.016);
            CHECK(frames() == initial + 2);
            checkbox->click();
            CHECK(!owner.LowPower() && !checkbox->isChecked() && !lowPowerMenu()->isChecked());
            initial = frames();
            for (int i = 0; i < 5; ++i)
                owner.Tick(.016);
            CHECK(frames() == initial + 5);
        }
        CHECK(owner.LastError().isEmpty() && !owner.isVisible() &&
              owner.RenderWindow()->Submissions() == 0);
    }
    {
        // Exercise the production modal path without touching user settings or
        // showing a native editor window. The test uses Qt's offscreen plugin.
        QTemporaryDir settings;
        CHECK(settings.isValid());
        const auto previousFormat = QSettings::defaultFormat();
        const auto previousOrganization = app.organizationName();
        const auto previousApplication = app.applicationName();
        app.setOrganizationName("MDS-Capacity-Test");
        app.setApplicationName("Offscreen");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        for (const char* language : {"en", "ja", "zh_CN"}) {
            qt::EditorWindow overflow(std::filesystem::absolute(argv[2]));
            CHECK(overflow.SetLanguage(language));
            qt::Language messages;
            CHECK(messages.Set(language));
            auto* frameTimer = overflow.findChild<QChronoTimer*>("editor.frameTimer");
            CHECK(frameTimer);
            frameTimer->stop();
            CHECK(overflow.Session().ImportModel(root / "articulated.pmx", reason));
            for (bool model : {false, true}) {
                CHECK(overflow.Session().Edit(
                    "capacity UI",
                    [&](auto& d, auto&) {
                        if (model)
                            FillKeyCapacity(d.models[0].displayKeys, 0);
                        else
                            FillKeyCapacity(d.cameraKeys, 0);
                        d.timeline.currentFrame = 21000;
                        return true;
                    },
                    reason));
                CHECK(overflow.Session().SelectModel(model ? 0 : 255, reason));
                overflow.Refresh();
                const auto frames = overflow.Session().Runtime().Counters().frames;
                CHECK(overflow.Session().Copy(
                    {{model ? Track::Display : Track::Camera, std::uint8_t(model ? 0 : 255), 0, 0}},
                    reason));
                for (bool paste : {false, true}) {
                    bool visited = false;
                    QTimer::singleShot(10, [&] {
                        auto* dialog =
                            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                        if (!dialog)
                            return;
                        const auto capacity = model ? 1000 : paste ? 10000 : 600000;
                        const auto text = messages
                                              .Text(model ? "dialog.model_key_capacity.message"
                                                          : "dialog.key_capacity.message")
                                              .arg(capacity);
                        visited =
                            dialog->text() == text && dialog->icon() == QMessageBox::NoIcon &&
                            dialog->windowTitle() == messages.Text("dialog.key_capacity.title") &&
                            dialog->standardButtons() == QMessageBox::Ok &&
                            dialog->parentWidget() == &overflow;
                        if (std::string(language) == "en")
                            visited &=
                                dialog->text() ==
                                (model
                                     ? "You cannot regist over 1000 point\nPlease execute 'delete unused frame'"
                                 : paste
                                     ? "You cannot regist over 10000point.\nPlease execute 'delete unused frame'"
                                     : "You cannot regist over 600000point.\nPlease execute 'delete unused frame'");
                        dialog->done(QMessageBox::Ok);
                    });
                    overflow.Dispatch(paste   ? panel::kPasteButton
                                      : model ? panel::kRegisterModelDisplayButton
                                              : panel::kRegisterCameraButton);
                    CHECK(visited && overflow.LastError().isEmpty() && overflow.Session().Dirty() &&
                          overflow.Session().Runtime().Counters().frames == frames &&
                          !overflow.isVisible() && overflow.RenderWindow()->Submissions() == 0);
                }
            }
        }
        QSettings::setDefaultFormat(previousFormat);
        app.setOrganizationName(previousOrganization);
        app.setApplicationName(previousApplication);
    }
    CHECK(!window.isVisible() && window.RenderWindow()->Submissions() == 0);
    std::cout
        << "Original binary menu tree/labels, translations, lifecycle and command state passed\n";
}
