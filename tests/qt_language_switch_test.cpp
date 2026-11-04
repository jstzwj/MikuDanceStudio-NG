#include "editor_window.hpp"
#include "editor_canvas.hpp"
#include "model_labels.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QStyleFactory>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

using namespace mikudancestudio;
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) try {
    QApplication app(argc, argv);
    Check(argc == 3 || argc == 4, "usage: assets model [production]");
    Check(QGuiApplication::platformName() == "offscreen" ||
              QGuiApplication::platformName() == "windows",
          "offscreen or never-shown Windows only");
    if (QGuiApplication::platformName() == "offscreen")
        app.setStyle(QStyleFactory::create("Fusion"));
    QTemporaryDir settings;
    Check(settings.isValid(), "isolated settings");
    QCoreApplication::setOrganizationName("MikuDanceStudio-language-test");
    QCoreApplication::setApplicationName("language-test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    qt::EditorWindow window(qt::Path(QString::fromLocal8Bit(argv[1])), argc != 4);
    window.findChild<QChronoTimer*>("editor.frameTimer")->stop();
    auto& session = window.Session();
    std::string reason;
    const auto model = qt::Path(QString::fromLocal8Bit(argv[2]));
    Check(session.ImportModel(model, reason), reason.c_str());
    Check(session.SelectBones({0}, 0, reason), reason.c_str());
    window.Refresh();
    qt::Canvas* canvas = nullptr;
    for (auto* widget : window.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<qt::Canvas*>(widget)) {
            canvas = candidate;
            break;
        }
    Check(canvas, "editor canvas");
    const auto resource = session.Runtime().Resource(0);
    const auto revision = session.Revision();
    const auto frames = session.Runtime().Counters().frames;
    const auto dirty = session.Dirty();
    // QWidget styles can lazily create their own overflow/context menus on
    // first paint. Compare the application-owned tree, not those Qt internals.
    const auto menus = [&] {
        QList<QMenu*> owned;
        for (auto* menu : window.findChildren<QMenu*>())
            if (menu->property("mmdTextId").isValid())
                owned.append(menu);
        return owned;
    };
    const auto actions = [&] {
        QList<QAction*> owned;
        for (auto* action : window.findChildren<QAction*>())
            if (action->property("mmdTextId").isValid() || action->property("rhiBackend").isValid())
                owned.append(action);
        return owned;
    };
    const auto originalMenus = menus();
    const auto originalActions = actions();
    for (const int method : {0, 1, 2})
        for (const char* code : {"en", "ja", "zh_CN", "en", "zh_CN", "ja"}) {
            if (method) {
                auto* languages = window.findChild<QMenu*>("menu.language");
                Check(languages && languages->actions().size() == 3, "language menu");
                const int index = std::string(code) == "en" ? 0 : std::string(code) == "ja" ? 1 : 2;
                QPointer<QMenu> menuGuard = languages;
                QPointer<QAction> action = languages->actions().at(index);
                if (method == 1)
                    action->trigger();
                else {
                    // Deliver only to the test's never-shown QWidgetWindow.
                    // This exercises Qt's mouse dispatch without OS input.
                    languages->ensurePolished();
                    languages->resize(languages->sizeHint());
                    languages->winId();
                    languages->setActiveAction(action);
                    const QPointF point = languages->actionGeometry(action).center();
                    const QPointF global = languages->mapToGlobal(point.toPoint());
                    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(languages->windowHandle(), &press);
                    QCoreApplication::sendEvent(languages->windowHandle(), &release);
                }
                Check(menuGuard && action && !languages->isVisible(),
                      "dispatch menu survives language activation and stays hidden");
            } else
                Check(window.SetLanguage(code), "set language");
            Check(window.CurrentLanguage() == code, "activation applied requested language");
            Check(menus() == originalMenus && actions() == originalActions,
                  "language changes preserve the menu tree without accumulating actions");
            const bool english = std::string(code) == "en";
            const auto expectedTitle = qt::ModelDisplayLabel(session, 0, english) +
                                       (english ? QStringLiteral(" : ") : QString(QChar(0xff1a))) +
                                       qt::ModelTrackLabel(session, 0, 0, false, english, true);
            Check(canvas->ViewportTitle() == expectedTitle,
                  "selected bone title uses localized names and original fullwidth separator");
            QImage capture(window.size(), QImage::Format_RGB32);
            window.render(&capture);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            Check(!window.isVisible(), "window stays hidden");
            Check(session.Revision() == revision && session.Dirty() == dirty &&
                      session.Runtime().Counters().frames == frames &&
                      session.Runtime().Resource(0) == resource,
                  "language switch preserves scene and runtime");
        }
    for (int cycle = 0; cycle < 4; ++cycle) {
        QPointer<QAction> originalToggle;
        for (auto* action : window.findChildren<QAction*>())
            if (action->property("mmdCommandId").toInt() == MDS_CMD_TOGGLE_UI_LANGUAGE)
                originalToggle = action;
        Check(bool(originalToggle), "original MMD language command");
        const auto expected = window.CurrentLanguage() == "en" ? "ja" : "en";
        originalToggle->trigger();
        Check(originalToggle && window.CurrentLanguage() == expected,
              "original language command retains its sender");
    }
    QPointer<QMenu> renderer = window.findChild<QMenu*>("menu.renderer");
    Check(bool(renderer), "renderer menu");
    for (auto* candidate : renderer->actions())
        if (candidate->isEnabled()) {
            QPointer<QAction> action = candidate;
            const auto backend = rhi::Backend(action->property("rhiBackend").toInt());
            action->trigger();
            Check(renderer && action && action->isChecked() &&
                      window.RenderWindow()->Options().backend == backend,
                  "backend selection retains the dispatch menu and updates checked state");
            for (auto* other : renderer->actions())
                Check(other->isChecked() == (other == action), "only selected backend is checked");
        }
    Check(session.Revision() == revision && session.Dirty() == dirty &&
              session.Runtime().Counters().frames == frames &&
              session.Runtime().Resource(0) == resource && !window.isVisible(),
          "menu commands preserve the loaded scene and hidden window");
    std::cout << "Loaded-model language switching passed: " << model.u8string() << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "language switch: " << error.what() << '\n';
    return 1;
}
