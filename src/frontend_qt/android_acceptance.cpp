#include "android_acceptance.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace mikudancestudio::qt {
int RunAndroidAcceptance(QApplication& app, EditorWindow& window, const QString& report) {
    int phase = 0;
    bool suspended = false, resumed = false;
    QElapsedTimer elapsed;
    elapsed.start();
    auto save = [&](const QString& state, const std::string& reason = {}) {
        QFile file(report);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        QJsonObject result{{"state", state},
                           {"version", app.applicationVersion()},
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
            viewport->destroy();
            viewport->create();
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
