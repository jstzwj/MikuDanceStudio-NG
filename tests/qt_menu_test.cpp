#include "editor_window.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenuBar>
#include <QPointer>
#include <QStyleFactory>
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
            CHECK(former.isNull());
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
    for (int id = 200; id <= 302; ++id)
        implemented += command(id)->property("mmdImplemented").toBool();
    CHECK(implemented == 51);
    CHECK(window.LastError().isEmpty());
    CHECK(!window.isVisible() && window.RenderWindow()->Submissions() == 0);
    std::cout
        << "Original binary menu tree/labels, translations, lifecycle and command state passed\n";
}
