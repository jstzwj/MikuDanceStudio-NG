#include "editor_window.hpp"
#include "mobile_editor.hpp"
#include <QApplication>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolBox>
#include <QTest>
#include <iostream>
using namespace mikudancestudio;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << '\n';                                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    qputenv("MDS_TEST_MOBILE_UI", "1");
    QApplication app(argc, argv);
    CHECK(argc == 3);
    CHECK(QGuiApplication::platformName() == "offscreen");
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    window.setMinimumSize(320, 240);
    window.resize(914, 411);
    window.show();
    app.processEvents();
    auto* mobile = window.findChild<QWidget*>("mobile.editor");
    auto* viewport = window.RenderWindow();
    CHECK(mobile && mobile->isVisible());
    CHECK(viewport->width() > 200 && viewport->height() > 150);
    auto* timeline = mobile->findChild<QPushButton*>("mobile.timeline");
    auto* controls = mobile->findChild<QPushButton*>("mobile.controls");
    CHECK(timeline && controls);
    QTest::mouseClick(timeline, Qt::LeftButton);
    app.processEvents();
    CHECK(timeline->isChecked() && !controls->isChecked());
    CHECK(window.TimelineWidget()->height() > 200);
    auto* frame = static_cast<QLineEdit*>(window.Control(panel::kCurrentFrameEdit));
    frame->setFocus();
    frame->selectAll();
    QTest::keyClicks(frame, "12");
    QTest::keyClick(frame, Qt::Key_Return);
    CHECK(window.Session().Document().timeline.currentFrame == 12);
    QTest::mouseClick(controls, Qt::LeftButton);
    app.processEvents();
    CHECK(!timeline->isChecked() && controls->isChecked());
    const int widgetCount = mobile->findChildren<QWidget*>().size();
    for (const auto& language : qt::Language::Available()) {
        CHECK(window.SetLanguage(language.code));
        window.Refresh();
        app.processEvents();
        CHECK(mobile->findChildren<QWidget*>().size() == widgetCount);
        CHECK(window.RenderWindow() == viewport);
    }
    CHECK(window.Open(std::filesystem::absolute(argv[1]) / "v2.pmm"));
    const auto* resource = window.Session().Assets().models.begin()->second.get();
    std::string reason;
    CHECK(window.Session().SelectModel(window.Session().Assets().models.begin()->first, reason));
    window.Refresh();
    app.processEvents();
    auto* tools = mobile->findChild<QToolBox*>();
    CHECK(tools && tools->count() >= 5);
    // All tabs plus one complete active panel must fit inside the scrollable
    // toolbox; otherwise following tabs cover the active panel's buttons.
    CHECK(tools->minimumHeight() >= tools->count() * 36 + 256);
    CHECK(qobject_cast<QScrollArea*>(tools->parentWidget()->parentWidget()));
    CHECK(!window.Control(panel::kBoneSelectRadio)->isHidden());
    CHECK(window.Control(panel::kPerspectiveCheckbox)->isHidden());
    CHECK(window.Session().Assets().models.begin()->second.get() == resource);
    window.resize(1280, 800);
    app.processEvents();
    CHECK(!mobile->isVisible());
    CHECK(window.Control(panel::kReadoutPosXEdit)->parentWidget() == window.centralWidget());
    CHECK(window.RenderWindow() == viewport);
    window.resize(914, 411);
    app.processEvents();
    CHECK(mobile->isVisible());
    CHECK(window.Session().Assets().models.begin()->second.get() == resource);
    CHECK(window.Session().Document().models.size() == 1);
    std::cout
        << "Mobile editor: drawer, frame editing, 15 languages, model mode, size transitions passed\n";
}
