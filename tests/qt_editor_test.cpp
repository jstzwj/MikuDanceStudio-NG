#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include <QApplication>
#include <QTest>
#include <QPushButton>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QStyleFactory>
#include <iostream>
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
    app.setStyle(QStyleFactory::create("Fusion"));
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    CHECK(!window.isVisible());
    CHECK(window.SetLanguage("en"));
    CHECK(window.Control(400));
    CHECK(window.Control(567));
    CHECK(qobject_cast<QAbstractButton*>(window.Control(400))->text() == "undo");
    CHECK(window.Control(400)->geometry().width() == 70);
    CHECK(window.Control(400)->geometry().y() == 21);
    CHECK(window.Session().ImportModel(root / "bone-only.pmd", reason));
    window.Refresh();
    const auto revision = window.Session().Revision(),
               updates = window.Session().Runtime().Counters().frames;
    CHECK(window.SetLanguage("ja"));
    CHECK(window.SetLanguage("zh_CN"));
    CHECK(window.Session().Revision() == revision);
    CHECK(window.Session().Runtime().Counters().frames == updates);
    CHECK(qobject_cast<QAbstractButton*>(window.Control(400))->text() ==
          QString::fromUtf8("\xe6\x92\xa4\xe9\x94\x80"));
    window.Session().Selected().bones = {0};
    CHECK(window.Session().SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
    window.Dispatch(panel::kRegisterFrameButton);
    CHECK(window.Session().Document().models[0].boneKeys.dense[0].position[1] == 2);
    window.Dispatch(panel::kNextFrameButton);
    CHECK(window.Session().Document().timeline.currentFrame == 1);
    window.Dispatch(panel::kCopyBonePoseButton);
    CHECK(window.Session().SetBone(0, 0, {4, 5, 6}, {0, 0, 0, 1}, reason));
    window.Dispatch(panel::kBonePasteButton);
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 2);
    window.Dispatch(panel::kUndoButton);
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 5);
    window.Session().Selected().model = 0;
    window.Session().Selected().bones = {0};
    window.Refresh();
    const auto cameraBefore = window.Session().Runtime().Session()->Pose().camera;
    auto* position = qobject_cast<QLineEdit*>(window.Control(panel::kReadoutPosXEdit));
    position->selectAll();
    QTest::keyClicks(position, "12.5");
    QTest::keyClick(position, Qt::Key_Return);
    CHECK(window.Session().Document().models[0].bonePose[0].components[0] == 12.5f);
    CHECK(window.Session().Runtime().Session()->Pose().camera.position == cameraBefore.position);
    auto* rotation = qobject_cast<QLineEdit*>(window.Control(panel::kReadoutRotYEdit));
    rotation->selectAll();
    QTest::keyClicks(rotation, "30");
    QTest::keyClick(rotation, Qt::Key_Return);
    const auto rotated = window.Session().Document().models[0].bonePose[0].components;
    CHECK(rotated[4] < 0 && rotated[6] > 0);
    const auto degrees =
        editor::BoneRotationDegrees({rotated[3], rotated[4], rotated[5], rotated[6]});
    CHECK(std::abs(degrees[1] - 30) < 1e-4f);
    CHECK(window.Session().Runtime().Session()->Pose().camera.rotation == cameraBefore.rotation);
    window.Dispatch(panel::kUndoButton);
    CHECK(window.Session().Document().models[0].bonePose[0].components[4] == 0);
    CHECK(window.Session().Document().models[0].bonePose[0].components[0] == 12.5f);
    CHECK(window.Save(root / "qt-save.pmm"));
    CHECK(window.Open(root / "qt-save.pmm"));
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 5);
    CHECK(!window.isVisible());
    CHECK(window.RenderWindow()->Submissions() == 0);
    std::cout
        << "Qt offscreen: original control positions, language state, edit/undo and saved reload passed\n";
}
