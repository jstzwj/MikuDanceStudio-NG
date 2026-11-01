#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/menu_commands.h"
#include "interpolation_raster_reference.hpp"
#include <QApplication>
#include <QTest>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QComboBox>
#include <QTemporaryDir>
#include <QStyleFactory>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QPainter>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QPointer>
#include <QCloseEvent>
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
#ifdef _WIN32
    // Qt's offscreen QPA has no native Windows font database. Load installed
    // fonts explicitly for raster/IME tests; never copy them into the repository.
    for (const char* name : {"arial.ttf", "msgothic.ttc", "msyh.ttc"})
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/" + name);
#endif
    app.setStyle(QStyleFactory::create("Fusion"));
    CHECK(argc == 3);
    unsigned rasterCase = 0;
    for (const auto& curve : kCurveInputs)
        for (bool highlight : {false, true}) {
            editor::InterpolationSelection selected;
            selected.curves = {curve};
            selected.uniform = highlight;
            const auto image = qt::InterpolationImage(selected);
            std::uint64_t hash = 14695981039346656037ull;
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                    for (int shift : {16, 8, 0})
                        hash = (hash ^ ((image.pixel(x, y) >> shift) & 255)) * 1099511628211ull;
            CHECK(hash == kCurveRasterHashes[rasterCase++]);
        }
    const auto root = std::filesystem::absolute(argv[1]);
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    CHECK(QFontMetrics(window.font()).inFont(QChar('A')));
    CHECK(QFontMetrics(window.font()).inFont(QChar('0')));
    CHECK(!window.isVisible());
    CHECK(window.SetLanguage("en"));
    {
        qt::EditorWindow detached(std::filesystem::absolute(argv[2]), true);
        auto& session = detached.Session();
        CHECK(session.ImportModel(root / "articulated.pmd", reason));
        CHECK(session.SetBone(0, 0, {2, 0, 0}, {0, 0, 0, 1}, reason));
        detached.centralWidget()->resize(1024, 700);
        session.SetSidebarWidth(381);
        detached.Refresh();
        const auto command = [&](int id) -> QAction* {
            for (auto* action : detached.findChildren<QAction*>())
                if (action->property("mmdCommandId").toInt() == id)
                    return action;
            return nullptr;
        };
        const auto* runtime = &session.Runtime();
        const auto* snapshot = session.Runtime().Snapshot(0);
        const auto bodies = session.Runtime().Bodies(0);
        const auto frames = session.Runtime().Counters().frames;
        const auto revision = session.Revision();
        const auto history = session.CanUndo();
        QPointer<qt::Viewport> former = detached.RenderWindow();
        command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
        CHECK(former.isNull());
        QPointer<QWidget> floating = detached.SeparateWindow();
        CHECK(floating && !floating->isVisible() && !detached.isVisible());
        CHECK(command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->isChecked());
        CHECK(session.Document().envelope.settings.editPanelWidth == 1021);
        for (const QSize size : {QSize(801, 658), QSize(640, 480), QSize(1281, 721)}) {
            floating->resize(size);
            detached.Refresh();
            const auto rect = ui::MmdSeparateViewport(size.width(), size.height(), 1024, 768);
            CHECK(detached.RenderWindow()->size() == QSize(rect.width, rect.height));
            for (const auto& control : ui::MmdViewportControls(-9, size.width(), rect)) {
                auto* widget = detached.Control(control.id);
                CHECK(widget->parentWidget() == floating);
                const auto height = qobject_cast<QComboBox*>(widget) ? 20 : control.height;
                CHECK(widget->geometry() == QRect(control.x, control.y, control.width, height));
                CHECK(!widget->isHidden());
            }
            CHECK(detached.Control(panel::kUndoButton)->parentWidget() == detached.centralWidget());
            for (const char* language : {"en", "ja", "zh_CN"}) {
                CHECK(detached.SetLanguage(language));
                CHECK(detached.SeparateWindow() == floating);
                CHECK(floating->size() == size);
                CHECK(floating->actions().size() == 2); // No obsolete shortcuts after rebuilding.
            }
        }
        command(MDS_CMD_TOGGLE_ALWAYS_ON_TOP)->trigger();
        CHECK(floating->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        CHECK(command(MDS_CMD_TOGGLE_ALWAYS_ON_TOP)->isChecked());
        CHECK(!floating->isVisible());
        command(MDS_CMD_TOGGLE_ALWAYS_ON_TOP)->trigger();
        CHECK(!floating->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        detached.centralWidget()->resize(1280, 700);
        detached.Refresh();
        CHECK(session.Document().envelope.settings.editPanelWidth == 1277);
        CHECK(&session.Runtime() == runtime && session.Runtime().Snapshot(0) == snapshot);
        CHECK(session.Runtime().Bodies(0) == bodies &&
              session.Runtime().Counters().frames == frames);
        CHECK(session.Revision() == revision && session.CanUndo() == history);
        // Header input follows the detached client origin and is cancelled on docking.
        const auto rect = ui::MmdSeparateViewport(floating->width(), floating->height(), 1024, 768);
        const QPointF zoom(floating->width() - 48, rect.y - 12);
        QMouseEvent press(QEvent::MouseButtonPress, zoom, zoom, zoom, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(floating, &press);
        CHECK(detached.RenderWindow()->HeaderDragging());
        former = detached.RenderWindow();
        QCloseEvent close;
        QCoreApplication::sendEvent(floating, &close);
        CHECK(!close.isAccepted() && !detached.SeparateWindow());
        CHECK(former.isNull() && !detached.RenderWindow()->HeaderDragging());
        CHECK(!command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->isChecked());
        CHECK(session.Document().envelope.settings.editPanelWidth == 381);
        for (int id = panel::kModelEditToggle; id <= panel::kCoordAxisCheckbox; ++id)
            CHECK(detached.Control(id)->parentWidget() == detached.centralWidget());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(floating.isNull());
        CHECK(&session.Runtime() == runtime && session.Runtime().Snapshot(0) == snapshot);
        CHECK(session.Runtime().Bodies(0) == bodies &&
              session.Runtime().Counters().frames == frames);
        CHECK(session.Revision() == revision && session.CanUndo() == history);
        CHECK(session.Play(reason));
        detached.Refresh();
        const auto playbackFrames = session.Runtime().Counters().frames;
        command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
        CHECK(session.Playing() && session.Runtime().Counters().frames == playbackFrames);
        CHECK(detached.SeparateWindow()->size() == QSize(1281, 721));
        detached.Tick(1. / 30);
        CHECK(session.Playing() && session.Runtime().Counters().frames == playbackFrames + 1);
        command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
        CHECK(session.Playing() && session.Runtime().Counters().frames == playbackFrames + 1);
        session.Pause();
        CHECK(session.UnloadModel(0, reason));
        detached.Refresh();
        command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
        CHECK(detached.SeparateWindow() && session.Document().models.empty());
        CHECK(!detached.isVisible() && !detached.SeparateWindow()->isVisible());
        CHECK(detached.RenderWindow()->Submissions() == 0 && detached.LastError().isEmpty());
        // Destruction while detached also owns the separate window and its controls.
    }
    {
        qt::EditorWindow clipboard(std::filesystem::absolute(argv[2]), true);
        auto& s = clipboard.Session();
        CHECK(!clipboard.Control(panel::kPasteButton)->isEnabled());
        CHECK(!clipboard.Control(panel::kReversePasteButton)->isEnabled());
        clipboard.Dispatch(panel::kCopyFramesButton); // Empty copy is not an error.
        CHECK(clipboard.LastError().isEmpty());
        CHECK(s.ImportModel(root / "triangle.pmd", reason));
        s.Selected().keys = {{editor::Track::Bone, 0, 0, 0}};
        clipboard.Dispatch(panel::kCopyFramesButton);
        CHECK(clipboard.Control(panel::kPasteButton)->isEnabled());
        CHECK(clipboard.Control(panel::kReversePasteButton)->isEnabled());
        CHECK(s.SelectModel(255, reason));
        clipboard.Refresh();
        CHECK(!clipboard.Control(panel::kPasteButton)->isEnabled());
        s.Selected().keys = {{editor::Track::Camera, 255, 0, 0}};
        clipboard.Dispatch(panel::kCopyFramesButton);
        CHECK(clipboard.Control(panel::kPasteButton)->isEnabled());
        CHECK(!clipboard.Control(panel::kReversePasteButton)->isEnabled());
        CHECK(s.SelectModel(0, reason));
        for (const char* language : {"en", "ja", "zh_CN"}) {
            CHECK(clipboard.SetLanguage(language));
            CHECK(clipboard.Control(panel::kPasteButton)->isEnabled());
            CHECK(clipboard.Control(panel::kReversePasteButton)->isEnabled());
        }
        CHECK(s.Play(reason));
        clipboard.Refresh();
        CHECK(!clipboard.Control(panel::kCopyFramesButton)->isEnabled());
        CHECK(!clipboard.Control(panel::kPasteButton)->isEnabled());
        CHECK(!clipboard.Control(panel::kReversePasteButton)->isEnabled());
        s.Pause();
        s.Selected().keys.clear();
        clipboard.Dispatch(panel::kCopyFramesButton);
        CHECK(!clipboard.Control(panel::kPasteButton)->isEnabled());
        CHECK(!clipboard.Control(panel::kReversePasteButton)->isEnabled());
        CHECK(s.SelectModel(255, reason));
        clipboard.Refresh();
        CHECK(clipboard.Control(panel::kPasteButton)->isEnabled());
        CHECK(!clipboard.Control(panel::kReversePasteButton)->isEnabled());
        CHECK(clipboard.LastError().isEmpty() && !clipboard.isVisible());
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        qt::EditorWindow idle(std::filesystem::absolute(argv[2]), true);
        auto& s = idle.Session();
        CHECK(s.ImportModel(root / "floor.pmx", reason));
        CHECK(s.Edit(
            "idle physics",
            [mode](auto& d, auto&) {
                d.physics.playbackMode = std::uint8_t(mode);
                d.appearance.floorVisible = 0;
                return true;
            },
            reason));
        idle.Refresh();
        const auto frames = s.Runtime().Counters().frames, revision = s.Revision();
        const auto bodies = s.Runtime().Bodies(0);
        idle.Tick(0);
        for (const char* language : {"en", "ja", "zh_CN"})
            CHECK(idle.SetLanguage(language));
        CHECK(s.Runtime().Counters().frames == frames && s.Runtime().Bodies(0) == bodies);
        for (unsigned i = 0; i < 60; ++i)
            idle.Tick(1. / 60);
        CHECK(s.Runtime().Counters().frames == frames + 60);
        CHECK(s.Revision() == revision && s.CursorSeconds() == 0 && !s.Playing());
        const auto result = s.Runtime().Bodies(0);
        CHECK(mode == 1 || mode == 2 ? result[0][13] < bodies[0][13] - 5 : result == bodies);
        CHECK(s.Play(reason));
        const auto playingFrames = s.Runtime().Counters().frames;
        idle.Tick(1. / 30);
        CHECK(s.Runtime().Counters().frames == playingFrames + 1);
        CHECK(s.CursorSeconds() == 1. / 30);
        s.Pause();
        idle.Refresh();
        const auto paused = s.CursorSeconds();
        idle.Tick(1. / 60);
        CHECK(s.Runtime().Counters().frames == playingFrames + 2 && s.CursorSeconds() == paused);
        CHECK(idle.LastError().isEmpty() && !idle.isVisible());
        CHECK(idle.RenderWindow()->Submissions() == 0);
    }
    {
        qt::EditorWindow registration(std::filesystem::absolute(argv[2]), true);
        auto& s = registration.Session();
        CHECK(s.ImportModel(root / "articulated.pmd", reason));
        registration.Refresh();
        auto* physics =
            static_cast<QAbstractButton*>(registration.Control(panel::kPhysicsCheckbox));
        CHECK(physics->isChecked() && s.RegistrationPhysics());
        const auto revision = s.Revision(), frames = s.Runtime().Counters().frames;
        const auto* runtime = &s.Runtime();
        const auto bodies = s.Runtime().Bodies(0);
        physics->click();
        CHECK(!physics->isChecked() && !s.RegistrationPhysics());
        for (const char* language : {"en", "ja", "zh_CN"}) {
            CHECK(registration.SetLanguage(language));
            CHECK(!physics->isChecked() && !s.RegistrationPhysics());
        }
        CHECK(s.Revision() == revision && &s.Runtime() == runtime);
        CHECK(s.Runtime().Counters().frames == frames && s.Runtime().Bodies(0) == bodies);
        CHECK(!s.CanUndo());
        s.Selected().bones = {0};
        registration.Dispatch(panel::kRegisterFrameButton);
        CHECK(registration.LastError().isEmpty());
        CHECK(s.Document().models[0].boneKeys.dense[0].physicsDisabled == 1);
        physics->click();
        registration.Dispatch(panel::kRegisterFrameButton);
        CHECK(registration.LastError().isEmpty());
        CHECK(s.Document().models[0].boneKeys.dense[0].physicsDisabled == 0);
        CHECK(!registration.isVisible() && !registration.RenderWindow()->isVisible());
    }
    {
        qt::EditorWindow divider(std::filesystem::absolute(argv[2]), true);
        auto* canvas = divider.centralWidget();
        CHECK(divider.Session().ImportModel(root / "pose.pmd", reason));
        canvas->resize(1024, 700);
        divider.Refresh();
        const auto frames = divider.Session().Runtime().Counters().frames;
        const auto revision = divider.Session().Revision();
        const auto* snapshot = divider.Session().Runtime().Snapshot(0);
        const auto mouse = [&](QEvent::Type type, QPointF point, Qt::MouseButton button,
                               Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, point, point, button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &event);
        };
        const auto drag = [&](int to) {
            const int width = divider.Session().Document().envelope.settings.editPanelWidth;
            mouse(QEvent::MouseButtonPress, {width + 2., 542}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseMove, {double(to), 542}, Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {double(to), 542}, Qt::LeftButton, Qt::NoButton);
        };
        mouse(QEvent::MouseMove, {252, 542}, Qt::NoButton, Qt::NoButton);
        CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
        drag(300);
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 300);
        CHECK(divider.Control(panel::kUndoButton)->x() == 70);
        const auto expected = ui::MmdViewport(1024, 700, 300, 1024, 768);
        CHECK(divider.RenderWindow()->width() == expected.width);
        canvas->resize(1280, 700);
        divider.Refresh();
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 375);
        canvas->resize(640, 700);
        divider.Refresh();
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 250);
        canvas->resize(1280, 700);
        divider.Refresh();
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 375);
        // Restore the original dimensions without changing the saved ratio.
        canvas->resize(1024, 700);
        divider.Refresh();
        drag(-100);
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 250);
        drag(5000);
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 1019);
        drag(381);
        CHECK(divider.SetLanguage("ja") && divider.SetLanguage("zh_CN"));
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 381);
        mouse(QEvent::MouseButtonPress, {383, 300}, Qt::LeftButton, Qt::LeftButton);
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&divider, &deactivate);
        mouse(QEvent::MouseMove, {500, 300}, Qt::NoButton, Qt::LeftButton);
        CHECK(divider.Session().Document().envelope.settings.editPanelWidth == 381);
        CHECK(divider.Session().Runtime().Counters().frames == frames);
        CHECK(divider.Session().Revision() == revision);
        CHECK(divider.Session().Runtime().Snapshot(0) == snapshot);
        CHECK(!divider.Session().CanUndo() && divider.RenderWindow()->Submissions() == 0);
        CHECK(divider.Save(root / "divider.pmm"));
        editor::EditorSession restored;
        CHECK(restored.Open(root / "divider.pmm", reason));
        CHECK(restored.Document().envelope.settings.editPanelWidth == 381);
        CHECK(!divider.isVisible());
    }
    {
        qt::EditorWindow header(std::filesystem::absolute(argv[2]), true);
        auto* canvas = header.centralWidget();
        canvas->resize(1024, 700);
        header.Refresh();
        auto* viewport = header.RenderWindow();
        const auto rect = ui::MmdViewport(1024, 700, 250, 1024, 768);
        const QPointF zoom(1024 - 48, rect.y - 12), pan(1024 - 18, rect.y - 12);
        const auto mouse = [&](QEvent::Type type, QPointF point, Qt::MouseButton button,
                               Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QMouseEvent event(type, point, point, point, button, buttons, mods);
            QCoreApplication::sendEvent(canvas, &event);
        };
        const auto before = header.Session().Document().cameraState;
        const auto frames = header.Session().Runtime().Counters().frames;
        mouse(QEvent::MouseButtonPress, zoom, Qt::LeftButton, Qt::LeftButton);
        CHECK(viewport->HeaderDragging() && viewport->cursor().shape() == Qt::BlankCursor);
        mouse(QEvent::MouseMove, zoom + QPointF(0, 10), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, zoom + QPointF(0, 10), Qt::LeftButton, Qt::NoButton);
        CHECK(!viewport->HeaderDragging() && viewport->cursor().shape() != Qt::BlankCursor);
        const auto ratio = viewport->devicePixelRatio();
        const float zoomDelta =
            float(qRound((zoom.y() + 10) * ratio) - qRound(zoom.y() * ratio)) * .1f;
        if (header.Session().Document().cameraState[5] != before[5] - zoomDelta)
            std::cerr << "header zoom before=" << before[5]
                      << " after=" << header.Session().Document().cameraState[5]
                      << " ratio=" << ratio << " delta=" << zoomDelta << '\n';
        CHECK(header.Session().Document().cameraState[5] == before[5] - zoomDelta);
        mouse(QEvent::MouseButtonPress, pan, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, pan + QPointF(10, -20), Qt::NoButton, Qt::LeftButton);
        const auto moved = header.Session().Document().cameraState;
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&header, &deactivate);
        CHECK(!viewport->HeaderDragging() && viewport->cursor().shape() != Qt::BlankCursor);
        mouse(QEvent::MouseMove, pan + QPointF(100, -200), Qt::NoButton, Qt::LeftButton);
        CHECK(header.Session().Document().cameraState == moved);
        CHECK(header.Session().Runtime().Counters().frames == frames &&
              !header.Session().CanUndo());
        CHECK(!header.isVisible() && viewport->Submissions() == 0 && header.LastError().isEmpty());
        CHECK(header.SetLanguage("ja") && header.SetLanguage("zh_CN"));
        CHECK(header.Session().Document().cameraState == moved);
        const auto sendView = [&](QEvent::Type type, QPointF point, Qt::MouseButton button,
                                  Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, point, point, button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &event);
        };
        sendView(QEvent::MouseButtonPress, {100, 100}, Qt::RightButton, Qt::RightButton);
        sendView(QEvent::MouseMove, {120, 90}, Qt::NoButton, Qt::RightButton);
        sendView(QEvent::MouseButtonRelease, {120, 90}, Qt::RightButton, Qt::NoButton);
        CHECK(std::abs(header.Session().Document().cameraState[6] - moved[6] -
                       float(qRound(100 * ratio) - qRound(90 * ratio)) * .01f) < 1e-6f);
        CHECK(std::abs(header.Session().Document().cameraState[7] - moved[7] +
                       float(qRound(120 * ratio) - qRound(100 * ratio)) * .01f) < 1e-6f);
        CHECK(header.Session().Runtime().Counters().frames == frames);
    }
    {
        const auto revision = window.Session().Revision();
        QTest::keyClick(&window, Qt::Key_L);
        CHECK(window.Session().CoordinateSystem() == 1);
        QTest::keyClick(&window, Qt::Key_L);
        CHECK(window.Session().CoordinateSystem() == 2);
        QTest::keyClick(&window, Qt::Key_L);
        CHECK(window.Session().CoordinateSystem() == 0);
        CHECK(window.Session().Revision() == revision);
    }
    {
        qt::EditorWindow modes(std::filesystem::absolute(argv[2]), true);
        auto& s = modes.Session();
        CHECK(s.ImportModel(root / "pose.pmd", reason));
        CHECK(s.ImportModel(root / "pose.pmd", reason));
        CHECK(s.Edit(
            "mode-camera",
            [](auto& d, auto&) {
                d.cameraKeys.dense[0].eye = {20, 0, 0};
                d.cameraKeys.dense[0].target = {};
                d.cameraKeys.dense[0].parentModel = -1;
                d.cameraState[0] = 123;
                d.lightState[0] = .875f;
                return true;
            },
            reason));
        CHECK(s.SetBone(1, 3, {9, 0, 0}, {0, 0, 0, 1}, reason));
        const auto* live = &s.Runtime();
        const auto* snapshot = live->Snapshot(1);
        const auto frames = live->Counters().frames;
        const int active = s.ActiveBone();
        modes.Refresh();
        auto* combo = static_cast<QComboBox*>(modes.Control(panel::kMainComboModel));
        combo->setCurrentIndex(combo->findData(0));
        combo->activated(combo->currentIndex());
        CHECK(s.Selected().model == 0);
        combo->setCurrentIndex(combo->findData(1));
        combo->activated(combo->currentIndex());
        CHECK(s.Selected().model == 1);
        combo->setCurrentIndex(0);
        combo->activated(0);
        CHECK(s.Selected().model == 255 && s.Document().cameraState[0] == 20);
        CHECK(s.Document().timeline.editMode == 2);
        modes.Dispatch(panel::kModelEditToggle);
        CHECK(s.Selected().model == 1 && s.Document().timeline.editMode == 0);
        CHECK(s.Document().lightState[0] == .602f);
        CHECK(s.ActiveBone() == active && s.Document().models[1].bonePose[3].components[0] == 9);
        CHECK(s.CanUndo());
        const auto state = s.Document().cameraState;
        const auto revision = s.Revision();
        for (const char* language : {"zh_CN", "ja", "en"}) {
            CHECK(modes.SetLanguage(language));
            CHECK(s.Selected().model == 1 && s.Document().cameraState == state);
            CHECK(s.Revision() == revision);
        }
        CHECK(&s.Runtime() == live && s.Runtime().Snapshot(1) == snapshot);
        CHECK(s.Runtime().Counters().frames == frames && modes.LastError().isEmpty());
        CHECK(s.Play(reason));
        modes.Refresh();
        CHECK(!combo->isEnabled() && !modes.Control(panel::kModelEditToggle)->isEnabled());
        s.Pause();
        modes.Refresh();
        CHECK(combo->isEnabled() && modes.Control(panel::kModelEditToggle)->isEnabled());
    }
    {
        qt::EditorWindow picking(std::filesystem::absolute(argv[2]), true);
        picking.centralWidget()->resize(1024, 700);
        auto& s = picking.Session();
        CHECK(s.ImportModel(root / "pose.pmd", reason));
        CHECK(s.SetBone(0, 3, {4, 0, 0}, {0, 0, 0, 1}, reason));
        picking.Refresh();
        auto* view = picking.RenderWindow();
        const auto ratio = view->devicePixelRatio();
        CHECK(s.PrepareView(reason));
        std::vector<cpu_bone::ScreenPoint> points;
        CHECK(render::SceneBoneScreenPoints(s.Runtime(), 0, unsigned(qRound(view->width() * ratio)),
                                            unsigned(qRound(view->height() * ratio)), points,
                                            reason, s.Selected().model));
        CHECK(points.size() == 8 && points[3].x != 393939);
        const QPointF hit(points[3].x / ratio, points[3].y / ratio);
        const auto click = [&](QEvent::Type type, QPointF point,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QMouseEvent event(type, point, point, Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                              modifiers);
            QApplication::sendEvent(view, &event);
        };
        const auto revision = s.Revision(), updates = s.Runtime().Counters().frames;
        click(QEvent::MouseButtonPress, hit);
        click(QEvent::MouseButtonRelease, hit);
        CHECK(picking.LastError().isEmpty());
        CHECK(s.Selected().bones == (std::vector<std::uint32_t>{3}));
        CHECK(s.Document().models[0].bonePose[3].flags[2] == 1);
        click(QEvent::MouseButtonPress, hit, Qt::ShiftModifier);
        click(QEvent::MouseButtonRelease, hit, Qt::ShiftModifier);
        CHECK(s.Selected().bones.empty());
        s.SetEditMode(1);
        click(QEvent::MouseButtonPress, hit - QPointF(9, 9));
        QMouseEvent move(QEvent::MouseMove, hit + QPointF(9, 9), hit + QPointF(9, 9), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &move);
        click(QEvent::MouseButtonRelease, hit + QPointF(9, 9));
        CHECK(s.Selected().bones == (std::vector<std::uint32_t>{3}));
        CHECK(s.Document().models[0].selection.selectedBone == 3);
        CHECK(s.Revision() == revision && s.Runtime().Counters().frames == updates);
        picking.Dispatch(panel::kSelectUnregisteredBonesButton);
        CHECK(s.Selected().bones == (std::vector<std::uint32_t>{3}));
        CHECK(s.Document().models[0].selection.selectedBone == -1);
        CHECK(s.Runtime().Counters().frames == updates);
        CHECK(!picking.isVisible() && !view->isVisible());
        const auto* liveRuntime = &s.Runtime();
        const auto* liveSnapshot = s.Runtime().Snapshot(0);
        const auto offsets = s.Document().cameraState;
        const auto dragView = [&](Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100),
                              button, button, modifiers);
            QApplication::sendEvent(view, &press);
            QMouseEvent move(QEvent::MouseMove, QPointF(120, 90), QPointF(120, 90), Qt::NoButton,
                             button, modifiers);
            QApplication::sendEvent(view, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(120, 90), QPointF(120, 90),
                                button, Qt::NoButton, modifiers);
            QApplication::sendEvent(view, &release);
        };
        dragView(Qt::RightButton, Qt::ShiftModifier);
        CHECK(s.Document().cameraState[3] == offsets[3] - 2 * ratio);
        CHECK(s.Document().cameraState[4] == offsets[4] - ratio);
        const auto distance = s.Document().cameraState[5];
        dragView(Qt::RightButton, Qt::ControlModifier);
        CHECK(s.Document().cameraState[5] == distance + ratio);
        const auto pitch = s.Document().cameraState[6], yaw = s.Document().cameraState[7];
        dragView(Qt::RightButton, Qt::NoModifier);
        CHECK(std::abs(s.Document().cameraState[6] - (pitch + .1f * ratio)) < 1e-6f);
        CHECK(std::abs(s.Document().cameraState[7] - (yaw - .2f * ratio)) < 1e-6f);
        QWheelEvent wheel(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view, &wheel);
        CHECK(s.Document().cameraState[5] == distance + ratio - 6);
        CHECK(&s.Runtime() == liveRuntime && s.Runtime().Snapshot(0) == liveSnapshot);
        CHECK(s.Runtime().Counters().frames == updates);
        CHECK(picking.LastError().isEmpty());
        CHECK(s.SelectBones({0, 3}, 3, reason));
        picking.Refresh();
        auto* activeX = qobject_cast<QLineEdit*>(picking.Control(panel::kReadoutPosXEdit));
        auto* activeRY = qobject_cast<QLineEdit*>(picking.Control(panel::kReadoutRotYEdit));
        CHECK(activeX->text() == "4.00" && activeRY->text() == "0.0");
        activeX->selectAll();
        QTest::keyClicks(activeX, "9.25");
        QTest::keyClick(activeX, Qt::Key_Return);
        CHECK(s.Document().models[0].bonePose[3].components[0] == 9.25f);
        CHECK(s.Document().models[0].bonePose[0].components[0] == 0);
        CHECK(s.SelectBones({0, 3}, -1, reason));
        picking.Refresh();
        CHECK(activeX->text() == "-----");
        const auto presetFrames = s.Runtime().Counters().frames;
        QKeyEvent keypad(QEvent::KeyPress, Qt::Key_4, Qt::KeypadModifier);
        QApplication::sendEvent(view, &keypad);
        CHECK(s.Document().cameraState[7] == 1.5707999467849731f);
        const auto presetRevision = s.Revision();
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_6, Qt::KeypadModifier, QString(), true);
        QApplication::sendEvent(view, &repeat);
        CHECK(s.Revision() == presetRevision);
        CHECK(s.Runtime().Counters().frames == presetFrames);
        const auto referenceFrames = s.Runtime().Counters().frames;
        const auto* referenceSnapshot = s.Runtime().Snapshot(0);
        auto* modelReference =
            qobject_cast<QAbstractButton*>(picking.Control(panel::kCameraRefModelCheckbox));
        auto* boneReference =
            qobject_cast<QAbstractButton*>(picking.Control(panel::kCameraRefBoneCheckbox));
        CHECK(modelReference && boneReference);
        modelReference->click();
        CHECK(s.Document().timeline.cameraReferenceMode == 1);
        CHECK(modelReference->isChecked() && !boneReference->isChecked());
        boneReference->click();
        CHECK(s.Document().timeline.cameraReferenceMode == 2);
        CHECK(!modelReference->isChecked() && boneReference->isChecked());
        boneReference->click();
        CHECK(s.Document().timeline.cameraReferenceMode == 0);
        CHECK(!modelReference->isChecked() && !boneReference->isChecked());
        CHECK(s.Runtime().Counters().frames == referenceFrames &&
              s.Runtime().Snapshot(0) == referenceSnapshot);
        auto* follow =
            qobject_cast<QAbstractButton*>(picking.Control(panel::kFollowCameraCheckbox));
        CHECK(follow && !follow->isChecked());
        follow->click();
        CHECK(s.Document().appearance.followCamera && follow->isChecked());
        follow->click();
        CHECK(!s.Document().appearance.followCamera && !follow->isChecked());
        CHECK(s.Runtime().Counters().frames == referenceFrames &&
              s.Runtime().Snapshot(0) == referenceSnapshot);
    }
    {
        qt::EditorWindow backgrounds(std::filesystem::absolute(argv[2]), true);
        CHECK(backgrounds.SetLanguage("en"));
        auto* show = backgrounds.findChild<QAction*>("background.show");
        CHECK(show && show->text() == "show background picture file(&P)");
        show->trigger();
        CHECK(!backgrounds.Session().Document().picture.enabled);
        CHECK(backgrounds.Session().ImportPicture(
            std::filesystem::absolute(argv[2]) / "hud_sprites.png", 640, 480, reason));
        backgrounds.Refresh();
        CHECK(show->isChecked());
        const auto picture = backgrounds.Session().Assets().picture;
        const auto before = backgrounds.Session().Revision();
        for (const char* language : {"ja", "zh_CN", "en"}) {
            CHECK(backgrounds.SetLanguage(language));
            CHECK(backgrounds.findChildren<QAction*>("background.show").size() == 1);
            CHECK(backgrounds.findChild<QAction*>("background.show")->isChecked());
            CHECK(backgrounds.Session().Revision() == before);
            CHECK(backgrounds.Session().Assets().picture == picture);
        }
        backgrounds.findChild<QAction*>("background.show")->trigger();
        CHECK(!backgrounds.Session().Document().picture.enabled);
        backgrounds.findChild<QAction*>("background.show")->trigger();
        CHECK(backgrounds.Session().Document().picture.enabled);
        for (unsigned i = 0; i < 4; ++i) {
            backgrounds.findChild<QAction*>(QString("background.capture.%1").arg(i))->trigger();
            CHECK(backgrounds.Session().Document().view.captureMode == int(i));
            for (unsigned j = 0; j < 4; ++j)
                CHECK(backgrounds.findChild<QAction*>(QString("background.capture.%1").arg(j))
                          ->isChecked() == (i == j));
        }
        backgrounds.findChild<QAction*>("background.black")->trigger();
        CHECK(backgrounds.Session().Document().appearance.blackBackground);
        CHECK(!backgrounds.isVisible());
    }
    CHECK(window.Control(400));
    CHECK(window.Control(567));
    CHECK(qobject_cast<QAbstractButton*>(window.Control(400))->text() == "undo");
    CHECK(window.Control(400)->geometry().width() == 70);
    CHECK(window.Control(400)->geometry().y() == 21);
    CHECK(window.Session().ImportModel(root / "bone-only.pmd", reason));
    window.Refresh();
    {
        auto* models = static_cast<QComboBox*>(window.Control(panel::kMainComboModel));
        auto* scopes = static_cast<QComboBox*>(window.Control(panel::kRegisterScopeCombo));
        QSignalSpy reset(models->model(), &QAbstractItemModel::modelReset);
        QSignalSpy inserted(scopes->model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(scopes->model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(models, &QComboBox::currentIndexChanged);
        for (unsigned i = 0; i < 5; ++i)
            window.Refresh();
        CHECK(reset.empty() && inserted.empty() && removed.empty() && changed.empty());
        CHECK(models->currentData().toUInt() == window.Session().Selected().model);
    }
    {
        window.Session().Selected().model = 0;
        window.Session().Selected().keys = {{editor::Track::Bone, 0, 0, 0}};
        window.Refresh();
        auto* combo = static_cast<QComboBox*>(window.Control(panel::kInterpCurveCombo));
        CHECK(combo->count() == 5 && combo->currentIndex() == 3);
        CHECK(combo->itemText(3) == "rotation");
        CHECK(window.Control(panel::kCurveCopyButton)->isEnabled());
        CHECK(!window.Control(panel::kCurvePasteButton)->isEnabled());
        auto* curve = window.InterpolationWidget();
        const auto before = window.Session().Revision();
        QTest::mousePress(curve, Qt::LeftButton, Qt::NoModifier, QPoint(20, 107));
        QMouseEvent move(QEvent::MouseMove, QPointF(50, 70), QPointF(50, 70), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(curve, &move);
        CHECK(window.Session().Interpolation(3).curves.front()[0] == 50);
        QTest::mouseRelease(curve, Qt::LeftButton, Qt::NoModifier, QPoint(70, 50));
        CHECK(window.Session().Interpolation(3).curves.front() ==
              (editor::InterpolationCurve{70, 77, 107, 107}));
        window.Dispatch(panel::kCurveCopyButton);
        CHECK(window.Control(panel::kCurvePasteButton)->isEnabled());
        window.Dispatch(panel::kUndoButton);
        CHECK(window.Session().Revision() > before);
        CHECK(window.Session().Interpolation(3).curves.front() ==
              (editor::InterpolationCurve{20, 20, 107, 107}));
        window.Dispatch(panel::kCurvePasteButton);
        CHECK(window.Session().Interpolation(3).curves.front()[0] == 70);
        window.Dispatch(panel::kCurveLinearButton);
        CHECK(window.Session().Interpolation(3).curves.front()[0] == 20);
        CHECK(!curve->isVisible());
    }
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
    CHECK(window.Control(panel::kBoneReversePasteButton)->isEnabled());
    window.Dispatch(panel::kBoneReversePasteButton);
    CHECK(window.Session().Document().models[0].bonePose[0].components[0] == -1);
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 2);
    window.Dispatch(panel::kResetBonePoseButton);
    CHECK(window.Session().Document().models[0].bonePose[0].components[0] == -1);
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 0);
    window.Dispatch(panel::kUndoButton);
    window.Dispatch(panel::kUndoButton);
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
    const auto beforeCollapse = window.Session().Revision();
    const auto beforeCollapseFrames = window.Session().Runtime().Counters().frames;
    window.Dispatch(panel::kCollapseBonePanelButton);
    CHECK(window.Session().Document().envelope.settings.options[4] == 0);
    CHECK(window.Control(panel::kBoneMoveRadio)->isHidden());
    CHECK(!window.Control(panel::kExpandBonePanelButton)->isHidden());
    CHECK(window.Session().Revision() == beforeCollapse);
    CHECK(window.Session().Runtime().Counters().frames == beforeCollapseFrames);
    window.Dispatch(panel::kExpandBonePanelButton);
    window.Dispatch(panel::kModelEditToggle);
    CHECK(window.Session().Selected().model == 255);
    CHECK(!window.Control(panel::kFovEdit)->isHidden());
    CHECK(window.Control(panel::kBoneMoveRadio)->isHidden());
    const auto enter = [&](int id, const char* value) {
        auto* e = qobject_cast<QLineEdit*>(window.Control(id));
        e->selectAll();
        QTest::keyClicks(e, value);
        QTest::keyClick(e, Qt::Key_Return);
    };
    enter(panel::kLightColorEditR, "128");
    CHECK(window.Session().Document().lightState[0] == .5f);
    static_cast<QSlider*>(window.Control(panel::kLightDirSliderX))->setValue(-25);
    CHECK(window.Session().Document().lightState[3] == -.25f);
    enter(panel::kSelfShadowRangeEdit, "8000");
    CHECK(window.Session().Document().physicsInterval == .02f);
    window.Dispatch(panel::kEditMode2Checkbox);
    CHECK(window.Session().Document().selfShadowMode == 2);
    CHECK(static_cast<QAbstractButton*>(window.Control(panel::kEditMode2Checkbox))->isChecked());
    CHECK(!static_cast<QAbstractButton*>(window.Control(panel::kEditMode1Checkbox))->isChecked());
    CHECK(window.Session().ImportAccessory(root / "bound.x", reason));
    window.Refresh();
    enter(panel::kAccPosXEdit, "3.25");
    enter(panel::kAccRotYEdit, "90");
    enter(panel::kAccOpacityEdit, "0.75");
    CHECK(window.Session().Document().accessories[0].state.position[0] == 3.25f);
    CHECK(std::abs(window.Session().Document().accessories[0].state.rotation[1] - 1.570796f) <
          1e-6f);
    CHECK(window.Session().Document().accessories[0].state.opacity == .75f);
    auto* parent = static_cast<QComboBox*>(window.Control(panel::kMainComboGround));
    parent->setCurrentIndex(parent->findData(0));
    parent->activated(parent->currentIndex());
    CHECK(window.Session().Document().accessories[0].state.parentModel == 0);
    window.Dispatch(panel::kCollapseLightPanelButton);
    CHECK(window.Control(panel::kLightColorEditR)->isHidden());
    CHECK(window.Save(root / "qt-save.pmm"));
    CHECK(window.Open(root / "qt-save.pmm"));
    CHECK(window.Session().Document().models[0].bonePose[0].components[1] == 5);
    CHECK(window.Session().Document().envelope.settings.options[2] == 0);
    CHECK(window.Session().Document().accessories[0].state.opacity == .75f);
    CHECK(!window.isVisible());
    CHECK(window.RenderWindow()->Submissions() == 0);
    CHECK(window.Session().New(reason));
    CHECK(window.Session().ImportModel(root / "grouped.pmx", reason));
    window.Refresh();
    auto* timeline = window.TimelineWidget();
    CHECK(timeline->RowCount() == 4);
    CHECK(window.Session().Document().models[0].groupCount == 4);
    const auto groupRevision = window.Session().Revision();
    const auto groupFrames = window.Session().Runtime().Counters().frames;
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(5, 65));
    CHECK(timeline->RowCount() == 5);
    CHECK(window.Session().Document().models[0].selection.groupFlags[3] == 1);
    CHECK(window.Session().Revision() == groupRevision);
    CHECK(window.Session().Runtime().Counters().frames == groupFrames);
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(30, 79));
    CHECK(window.Session().Selected().bones == std::vector<std::uint32_t>{0});
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::ControlModifier, QPoint(30, 79));
    CHECK(window.Session().Selected().bones.empty());
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(5, 65));
    CHECK(timeline->RowCount() == 4);
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 65));
    CHECK(window.Session().Selected().keys.count({editor::Track::Bone, 0, 0, 0}) == 1);
    CHECK(window.Session().SetBone(0, 0, {2, 0, 0}, {0, 0, 0, 1}, reason));
    window.Session().SetGroupExpanded(0, 3, true);
    CHECK(window.Session().Undo(reason));
    CHECK(window.Session().Document().models[0].selection.groupFlags[3] == 1);
    CHECK(window.Save(root / "qt-groups.pmm") && window.Open(root / "qt-groups.pmm"));
    CHECK(window.Session().Document().models[0].selection.groupFlags[3] == 1);
    QStringList catalogKeys;
    CHECK(window.SetLanguage("en"));
    auto* scope = static_cast<QComboBox*>(window.Control(panel::kRegisterScopeCombo));
    CHECK(scope->itemText(0) == "All frame");
    CHECK(scope->itemText(3) == "Sel Bone");
    CHECK(scope->findText("All facial") == 5);
    CHECK(scope->findText("All bone") == 7);
    CHECK(window.Session().Register(
        {{editor::Track::Bone, 0, 0, 10}, {editor::Track::Morph, 0, 0, 10}}, reason));
    window.Session().Selected().keys.clear();
    static_cast<QLineEdit*>(window.Control(panel::kRangeStartEdit))->setText("10");
    static_cast<QLineEdit*>(window.Control(panel::kRangeStopEdit))->setText("10");
    scope->setCurrentIndex(5);
    scope->activated(5);
    window.Dispatch(panel::kSelectRangeButton);
    CHECK(window.Session().Selected().keys.size() == 1);
    CHECK(window.Session().Selected().keys.count({editor::Track::Morph, 0, 0, 10}));
    CHECK(window.Session().Seek(99, reason));
    window.Dispatch(panel::kShowCurrentFrameButton);
    CHECK(timeline->FrameScroll() == 99 - (250 - 84) / 26);
    window.Dispatch(panel::kSelectColumnButton);
    CHECK(window.Session().Selected().keys.size() == 2);
    window.Dispatch(panel::kModelEditToggle);
    CHECK(scope->count() == 4 && scope->currentIndex() == 0);
    window.Dispatch(panel::kModelEditToggle);
    CHECK(scope->currentIndex() == 5);
    CHECK(window.SetLanguage("ja"));
    CHECK(scope->currentIndex() == 5);
    for (const char* language : {"en", "ja", "zh_CN"}) {
        QFile catalog(QString(":/i18n/%1.json").arg(language));
        CHECK(catalog.open(QIODevice::ReadOnly));
        const auto data = QJsonDocument::fromJson(catalog.readAll()).object();
        if (catalogKeys.isEmpty())
            catalogKeys = data.keys();
        CHECK(catalogKeys == data.keys());
        for (const auto& key : catalogKeys) {
            CHECK(data[key].isString());
            CHECK(!data[key].toString().contains("??"));
        }
    }
    const auto artifact = qEnvironmentVariable("MDS_QT_ARTIFACT");
    if (!artifact.isEmpty()) {
        window.resize(1280, 800);
        QImage pixels(window.size(), QImage::Format_ARGB32);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        window.render(&painter);
        painter.end();
        CHECK(pixels.save(artifact));
        CHECK(!window.isVisible());
    }
    std::cout
        << "Qt offscreen: original control positions, language state, edit/undo and saved reload passed\n";
}
