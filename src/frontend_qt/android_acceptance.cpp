#include "android_acceptance.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QInputMethod>
#include <QLineEdit>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QJsonArray>
#include <QTouchEvent>

namespace mikudancestudio::qt {
namespace {
class InputObserver final : public QObject {
public:
    using QObject::QObject;
    QJsonArray events;
    bool eventFilter(QObject* target, QEvent* event) override {
        if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchEnd) {
            const auto* touch = static_cast<QTouchEvent*>(event);
            const auto point =
                touch->points().isEmpty() ? QPointF() : touch->points().front().position();
            events.append(QJsonObject{{"type", int(event->type())},
                                      {"class", target->metaObject()->className()},
                                      {"x", point.x()},
                                      {"y", point.y()}});
            while (events.size() > 4)
                events.removeFirst();
        }
        auto* widget = qobject_cast<QWidget*>(target);
        if (widget && (event->type() == QEvent::MouseButtonPress ||
                       event->type() == QEvent::MouseButtonRelease)) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const auto* button = qobject_cast<QAbstractButton*>(widget);
            const auto origin = widget->mapToGlobal(QPoint());
            events.append(QJsonObject{{"type", int(event->type())},
                                      {"widget", widget->objectName()},
                                      {"class", widget->metaObject()->className()},
                                      {"text", button ? button->text() : QString()},
                                      {"x", mouse->globalPosition().x()},
                                      {"y", mouse->globalPosition().y()},
                                      {"left", origin.x()},
                                      {"top", origin.y()}});
            while (events.size() > 4)
                events.removeFirst();
        }
        return false;
    }
};
}
void StartAndroidUiReport(EditorWindow& window, const QString& report) {
    // Opt-in observer of the ordinary editor. It never injects commands, changes
    // the document, renders a separate scene or changes the application's mode.
    auto* timer = new QTimer(&window);
    auto* input = new InputObserver(&window);
    qApp->installEventFilter(input);
    QObject::connect(timer, &QTimer::timeout, &window, [&window, report, input] {
        const auto& session = window.Session();
        const auto& camera = session.Runtime().Session()->Pose().camera;
        auto* focus = QApplication::focusWidget();
        const auto edit = qobject_cast<QLineEdit*>(focus);
        QJsonArray nativeWindows;
        for (auto* native : QGuiApplication::allWindows())
            nativeWindows.append(QJsonObject{{"name", native->objectName()},
                                             {"visible", native->isVisible()},
                                             {"exposed", native->isExposed()},
                                             {"focused", native == QGuiApplication::focusWindow()},
                                             {"type", int(native->type())}});
        QJsonObject result{
            {"windows", nativeWindows},
            {"editorVisible", window.isVisible()},
            {"language", window.CurrentLanguage()},
            {"models", int(session.Assets().models.size())},
            {"selectedModel", session.Selected().model},
            {"frame", session.Document().timeline.currentFrame},
            {"playing", session.Playing()},
            {"revision", qint64(session.Revision())},
            {"cameraX", camera.rotation[0]},
            {"cameraY", camera.rotation[1]},
            {"submissions", qint64(window.RenderWindow()->Submissions())},
            {"surfaceRebuilds", qint64(window.RenderWindow()->SurfaceRebuilds())},
            {"presentationState", int(window.RenderWindow()->PresentationState())},
            {"presentationReason",
             QString::fromStdString(window.RenderWindow()->PresentationReason())},
            {"presentationBlocked", window.RenderWindow()->presentationBlocked &&
                                        window.RenderWindow()->presentationBlocked()},
            {"frameBuilds", qint64(window.RenderWindow()->FrameBuilds())},
            {"viewportVisible", window.RenderWindow()->isVisible()},
            {"viewportExposed", window.RenderWindow()->isExposed()},
            {"applicationState", int(QGuiApplication::applicationState())},
            {"modal", QApplication::activeModalWidget()
                          ? QApplication::activeModalWidget()->objectName()
                          : QString()},
            {"popup", QApplication::activePopupWidget()
                          ? QApplication::activePopupWidget()->metaObject()->className()
                          : QString()},
            {"viewportWidth", window.RenderWindow()->width()},
            {"viewportHeight", window.RenderWindow()->height()},
            {"imeVisible", QGuiApplication::inputMethod()->isVisible()},
            {"focus", focus ? focus->objectName() : QString()},
            {"focusText", edit ? edit->text() : QString()},
            {"input", input->events},
            {"error", window.LastError()}};
        QFile file(report);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(result).toJson());
    });
    timer->start(250);
}
int RunAndroidAcceptance(QApplication& app, EditorWindow& window, const QString& report) {
    int phase = 0;
    qint64 nextProgress = 0;
    bool suspended = false, resumed = false;
    QElapsedTimer elapsed;
    elapsed.start();
    auto save = [&](const QString& state, const std::string& reason = {}) {
        QFile file(report);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        QJsonObject result{{"state", state},
                           {"version", app.applicationVersion()},
                           {"phase", phase},
                           {"applicationState", int(app.applicationState())},
                           {"windowVisible", window.isVisible()},
                           {"viewportVisible", window.RenderWindow()->isVisible()},
                           {"viewportExposed", window.RenderWindow()->isExposed()},
                           {"width", window.RenderWindow()->width()},
                           {"height", window.RenderWindow()->height()},
                           {"submissions", qint64(window.RenderWindow()->Submissions())},
                           {"suspended", suspended},
                           {"resumed", resumed},
                           {"reason", QString::fromStdString(reason)}};
        return file.write(QJsonDocument(result).toJson()) > 0;
    };
    const auto stateConnection = QObject::connect(
        &app, &QGuiApplication::applicationStateChanged, [&](Qt::ApplicationState state) {
            if (phase == 2 && state != Qt::ApplicationActive)
                suspended = true;
            if (phase == 2 && suspended && state == Qt::ApplicationActive)
                resumed = true;
        });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (elapsed.elapsed() > 120000) {
            save("failed", "android-native-presentation-timeout");
            app.exit(10);
            return;
        }
        if (phase < 2 && elapsed.elapsed() >= nextProgress) {
            save("await-surface");
            nextProgress = elapsed.elapsed() + 1000;
        }
        if (app.applicationState() != Qt::ApplicationActive)
            return;
        std::string reason;
        auto* viewport = window.RenderWindow();
        if (!viewport->Render(reason)) {
            save("failed", reason);
            app.exit(11);
            return;
        }
        if (viewport->Submissions() < 3)
            return;
        if (phase == 0) {
            // Native surface retirement and reacquisition use the ordinary
            // editor path. Retain the PMM/model state across replacement.
            viewport->hide();
            viewport->destroy();
            viewport->create();
            viewport->show();
            phase = 1;
        } else if (phase == 1) {
            if (!viewport->Reset(reason)) {
                save("failed", reason);
                app.exit(12);
                return;
            }
            phase = 2;
            save("await-lifecycle");
        } else if (phase == 2 && resumed) {
            save("passed");
            timer.stop();
            // The external emulator gate captures the actual presented view
            // before stopping this isolated process.
        }
    });
    window.show();
    timer.start(50);
    const auto result = app.exec();
    QObject::disconnect(stateConnection);
    return result;
}
}
