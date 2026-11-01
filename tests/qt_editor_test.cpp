#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
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
    CHECK(window.Save(root / "qt-save.pmm"));
    CHECK(window.Open(root / "qt-save.pmm"));
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 5);
    CHECK(!window.isVisible());
    CHECK(window.RenderWindow()->Submissions() == 0);
    std::cout
        << "Qt offscreen: original control positions, language state, edit/undo and saved reload passed\n";
}
