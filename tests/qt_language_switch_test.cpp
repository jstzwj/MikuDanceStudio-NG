#include "editor_window.hpp"
#include "editor_canvas.hpp"
#include "model_labels.hpp"
#include "preferences_dialog.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QStyleFactory>
#include <QSettings>
#include <QTemporaryDir>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDir>
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
    Check(!window.findChild<QMenu*>("menu.language"), "language settings moved to preferences");
    qt::Language independent;
    Check(independent.Set("en"), "independent English catalog");
    Check(qt::Language::Resolve("zh-Hant-HK") == "zh_TW" &&
              qt::Language::Resolve("zh-Hans-SG") == "zh_CN" &&
              qt::Language::Resolve("fr-CA") == "fr" && qt::Language::Resolve("de-AT") == "de" &&
              qt::Language::Resolve("unsupported") == "en",
          "regional locale resolution");
    Check(!independent.Set("unsupported") && independent.Code() == "en",
          "invalid language leaves loaded catalog intact");
    for (const int method : {0, 1, 2})
        for (const char* code :
             {"en", "ja", "zh_CN", "zh_TW", "ko", "fr", "de", "es", "en", "ja"}) {
            if (method) {
                auto* file = window.findChild<QMenu*>("menu.file");
                QPointer<QMenu> menuGuard = file;
                QPointer<QAction> action = window.findChild<QAction*>("file.preferences");
                Check(file && action && file->actions().contains(action), "File preferences entry");
                if (method == 1)
                    action->trigger();
                else {
                    // Deliver only to the test's never-shown QWidgetWindow.
                    // This exercises Qt's mouse dispatch without OS input.
                    file->ensurePolished();
                    file->resize(file->sizeHint());
                    file->winId();
                    file->setActiveAction(action);
                    const QPointF point = file->actionGeometry(action).center();
                    const QPointF global = file->mapToGlobal(point.toPoint());
                    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(file->windowHandle(), &press);
                    QCoreApplication::sendEvent(file->windowHandle(), &release);
                }
                auto* dialog = window.findChild<QDialog*>("preferences");
                Check(dialog && !dialog->isVisible(),
                      "preferences constructed without desktop activation");
                auto* combo = dialog->findChild<QComboBox*>("preferences.language");
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                Check(combo && buttons && combo->count() == qt::Language::Available().size() + 1,
                      "native language names and system option");
                const auto previous = window.CurrentLanguage();
                combo->setCurrentIndex(combo->findData(code));
                Check(window.CurrentLanguage() == previous,
                      "selection is staged until Apply or OK");
                buttons->button(method == 1 ? QDialogButtonBox::Apply : QDialogButtonBox::Ok)
                    ->click();
                Check(menuGuard && action && !file->isVisible() && !dialog->isVisible(),
                      "dispatch menu and preferences survive language activation and stay hidden");
            } else
                Check(window.SetLanguage(code), "set language");
            Check(window.CurrentLanguage() == code, "activation applied requested language");
            Check(menus() == originalMenus && actions() == originalActions,
                  "language changes preserve the menu tree without accumulating actions");
            const bool english = std::string(code) != "ja" && std::string(code).find("zh") != 0;
            const auto expectedTitle = qt::ModelDisplayLabel(session, 0, english) +
                                       (english ? QStringLiteral(" : ") : QString(QChar(0xff1a))) +
                                       qt::ModelTrackLabel(session, 0, 0, false, english, true);
            Check(canvas->ViewportTitle() == expectedTitle,
                  "selected bone title uses localized names and original fullwidth separator");
            QImage capture(window.size(), QImage::Format_RGB32);
            window.render(&capture);
            const auto artifacts = qEnvironmentVariable("MDS_TRANSLATION_ARTIFACT_DIR");
            if (method == 1 && !artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                Check(capture.save(QDir(artifacts).filePath(QString(code) + "-editor.png")),
                      "save hidden translated editor capture");
                auto* preferences = window.findChild<QDialog*>("preferences");
                preferences->ensurePolished();
                preferences->resize(preferences->sizeHint());
                QImage image(preferences->size(), QImage::Format_RGB32);
                preferences->render(&image);
                Check(image.save(QDir(artifacts).filePath(QString(code) + "-preferences.png")),
                      "save hidden preferences capture");
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            Check(!window.isVisible(), "window stays hidden");
            Check(session.Revision() == revision && session.Dirty() == dirty &&
                      session.Runtime().Counters().frames == frames &&
                      session.Runtime().Resource(0) == resource,
                  "language switch preserves scene and runtime");
            Check(independent.Text("menu.file") == "file(&F)", "local catalogs remain independent");
            Check(qtTrId("preferences.title") ==
                      window.findChild<QAction*>("file.preferences")->text().remove(QChar(0x2026)),
                  "Qt application translator installed");
            if (argc == 4)
                Check(QSettings().value("language").toString() == code,
                      "language setting persists");
        }
    auto* dialog = window.findChild<QDialog*>("preferences");
    auto* combo = dialog->findChild<QComboBox*>("preferences.language");
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    const auto current = window.CurrentLanguage();
    combo->setCurrentIndex(combo->findData("de"));
    buttons->button(QDialogButtonBox::Cancel)->click();
    Check(window.CurrentLanguage() == current, "Cancel discards staged language selection");
    window.findChild<QAction*>("file.preferences")->trigger();
    Check(combo->currentData().toString() == window.LanguageSelection(),
          "reopening resets cancelled selection");
    combo->setCurrentIndex(combo->findData("system"));
    buttons->button(QDialogButtonBox::Apply)->click();
    Check(window.LanguageSelection() == "system" &&
              window.CurrentLanguage() == qt::Language::Resolve("system"),
          "system preference retains selection and resolves effective language");
    if (argc == 4) {
        Check(QSettings().value("language").toString() == "system", "persist system selection");
        QSettings().setValue("language", "fr-CA");
        qt::EditorWindow restarted(qt::Path(QString::fromLocal8Bit(argv[1])), false);
        restarted.findChild<QChronoTimer*>("editor.frameTimer")->stop();
        Check(restarted.CurrentLanguage() == "fr" && !restarted.isVisible(),
              "restart restores regional preference without showing window");
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
