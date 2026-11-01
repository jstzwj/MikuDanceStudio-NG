#include "viewport.hpp"
#include "mikudancestudio/background_placement.hpp"
#include <QApplication>
#include <QMouseEvent>
#include <QFocusEvent>
#include <iostream>
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
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    editor::EditorSession session;
    CHECK(session.ImportModel(root / "drag.pmd", reason));
    CHECK(session.SelectBones({0, 1}, 0, reason));
    qt::Viewport viewport(session, std::filesystem::absolute(argv[2]));
    viewport.resize(640, 480);
    bool ok = true;
    unsigned changes = 0;
    viewport.changed = [&](bool result, const std::string& error) {
        ok &= result;
        reason = error;
        ++changes;
    };
    const auto mouse = [&](QEvent::Type type, QPointF p, Qt::MouseButton button,
                           Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent event(type, p, p, p, button, buttons, mods);
        QCoreApplication::sendEvent(&viewport, &event);
    };
    const auto axis = [&](int column, bool upper) {
        const auto ratio = viewport.devicePixelRatio();
        const auto width = qRound(viewport.width() * ratio),
                   height = qRound(viewport.height() * ratio);
        const float scale = render::BackgroundViewScale(width);
        return QPointF((width - (110 - 40 * column) * scale) / ratio,
                       (height - (upper ? 65 : 25) * scale) / ratio);
    };
    const auto start = axis(0, false);
    const auto initial = session.Document().models[0].bonePose[0].components;
    mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    CHECK(session.CanUndo()); // press snapshot, before first movement
    mouse(QEvent::MouseMove, start - QPointF(0, 10), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, start - QPointF(0, 20), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, start - QPointF(0, 20), Qt::LeftButton, Qt::NoButton);
    CHECK(ok && changes >= 4);
    CHECK(std::abs(session.Document().models[0].bonePose[0].components[0] -
                   viewport.devicePixelRatio()) < 1e-6);
    CHECK(session.Document().models[0].bonePose[1].components[0] == 0);
    CHECK(session.Undo(reason));
    CHECK(session.Document().models[0].bonePose[0].components == initial && !session.CanUndo());
    const auto rotate = axis(1, true);
    mouse(QEvent::MouseButtonPress, rotate, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, rotate - QPointF(0, 10), Qt::NoButton, Qt::LeftButton,
          Qt::ControlModifier);
    QFocusEvent focus(QEvent::FocusOut);
    QCoreApplication::sendEvent(&viewport, &focus);
    CHECK(ok && session.BeginGesture()); // focus loss must end drag and gesture
    session.EndGesture();
    CHECK(session.Undo(reason));
    CHECK(session.Document().models[0].bonePose[0].components == initial);
    // Direct cursor movement enters temporary ToolDrag and returns to Move on release.
    CHECK(session.PrepareView(reason));
    std::vector<cpu_bone::ScreenPoint> points;
    const auto ratio = viewport.devicePixelRatio();
    CHECK(render::SceneBoneScreenPoints(session.Runtime(), 0, unsigned(qRound(640 * ratio)),
                                        unsigned(qRound(480 * ratio)), points, reason, 0));
    session.SetEditMode(4);
    const QPointF handle((points[0].x + 50) / ratio, points[0].y / ratio);
    mouse(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
    CHECK(session.Document().timeline.editMode == 5);
    mouse(QEvent::MouseMove, handle + QPointF(10, 0), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, handle + QPointF(10, 0), Qt::LeftButton, Qt::NoButton);
    CHECK(ok && session.Document().timeline.editMode == 4);
    CHECK(session.Undo(reason));
    CHECK(session.Document().models[0].bonePose[0].components == initial);
    // Picking and a backwards box at fractional DPI use the same physical projection.
    CHECK(session.SelectBones({}, -1, reason));
    session.SetEditMode(0);
    const QPointF center(points[0].x / ratio, points[0].y / ratio);
    mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    CHECK(session.ActiveBone() >= 0 && !session.Selected().bones.empty());
    session.SetEditMode(1);
    const QPointF near((points[0].x - 10) / ratio, (points[0].y - 10) / ratio);
    const QPointF far((points[0].x + 10) / ratio, (points[0].y + 10) / ratio);
    mouse(QEvent::MouseButtonPress, far, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, near, Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, near, Qt::LeftButton, Qt::NoButton);
    CHECK(ok && session.Selected().bones == (std::vector<std::uint32_t>{0}));
    CHECK(session.ActiveBone() == 0);
    CHECK(!viewport.isVisible() && viewport.Submissions() == 0);
    std::cout
        << "Qt synthetic bone gestures, axis hit, selection inheritance, focus loss, undo and DPR passed without a surface\n";
}
