#include "android_acceptance.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
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
#include <algorithm>

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
        const auto& assets = session.Assets();
        std::int64_t videoTime = 0;
        const bool videoCurrent = media::VideoTime(session.RequestVideo().seconds, videoTime) &&
                                  assets.video.frozen.Covers(videoTime);
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
            {"waveLoaded", !assets.wave.empty()},
            {"videoVisible", session.Document().avi.enabled != 0},
            {"videoCurrent", videoCurrent},
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
int RunAndroidAcceptance(QApplication& app, EditorWindow& window, const QString& report,
                         const QString& mediaDirectory) {
    int phase = 0;
    qint64 nextProgress = 0;
    bool suspended = false, resumed = false;
    bool mediaPlayed = false, mediaSought = false;
    auto& session = window.Session();
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
                           {"mediaPlayed", mediaPlayed},
                           {"mediaSought", mediaSought},
                           {"reason", QString::fromStdString(reason)}};
        return file.write(QJsonDocument(result).toJson()) > 0;
    };
    if (!mediaDirectory.isEmpty()) {
        const auto fixture = Path(mediaDirectory);
        std::string reason;
        VideoDecoder decoder;
        media::VideoFrame first, later, again;
        if (!decoder.Open(fixture / "mobile.avi", reason) ||
            !decoder.ReadScene(0, true, first, {}, reason) ||
            !decoder.ReadScene(1, true, later, {}, reason) ||
            !decoder.ReadScene(0, true, again, {}, reason) ||
            first.image->pixels == later.image->pixels ||
            first.image->pixels != again.image->pixels ||
            !session.ImportVideo(fixture / "mobile.avi", decoder.Info().width,
                                 decoder.Info().height, 160, 96, reason) ||
            !session.ImportWave(fixture / "mobile.wav", reason)) {
            save("failed", reason.empty() ? "android-media-seek-mismatch" : reason);
            return 14;
        }
    }
    qint64 lastTick = 0;
    bool startedMedia = false;
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
            if (!mediaDirectory.isEmpty() && !mediaSought) {
                const auto now = elapsed.elapsed();
                if (!startedMedia) {
                    window.Dispatch(panel::kPlayButton);
                    startedMedia = true;
                    lastTick = now;
                    return;
                }
                if (!mediaPlayed) {
                    window.Tick(std::min(1., (now - lastTick) / 1000.));
                    lastTick = now;
                    if (!session.Playing() || !window.LastError().isEmpty()) {
                        save("failed", window.LastError().toStdString());
                        app.exit(15);
                        return;
                    }
                    if (session.CursorSeconds() < .3)
                        return;
                    mediaPlayed = true;
                    window.Dispatch(panel::kPlayButton);
                    if (!session.Seek(31, reason)) {
                        save("failed", reason);
                        app.exit(16);
                    }
                    return;
                }
                if (!session.Assets().video.frozen.Covers(1033333))
                    return;
                mediaSought = true;
                if (!window.Save(Path(report + ".pmm"))) {
                    save("failed", window.LastError().toStdString());
                    app.exit(16);
                    return;
                }
            }
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
            if (!mediaDirectory.isEmpty() &&
                (!session.Assets().video.frozen.Covers(1033333) || session.Assets().wave.empty())) {
                save("failed", "android-media-state-lost");
                app.exit(17);
                return;
            }
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
