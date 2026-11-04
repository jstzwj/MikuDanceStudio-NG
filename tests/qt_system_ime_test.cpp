// Real platform input context qualification. Visible only in an isolated CI display.
#ifdef _WIN32
#include "windows_ime_session.hpp"
#endif
#include "english_name_dialog.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include <QApplication>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>
#ifdef __APPLE__
void ActivateCocoaInputWindow(std::uintptr_t);
std::string ActivateCocoaJapanese();
#endif

using namespace mikudancestudio;
namespace {
struct LifetimeTrace {
    const char* name;
    ~LifetimeTrace() {
        std::fprintf(stderr, "IME destroyed %s\n", name);
        std::fflush(stderr);
    }
};
#ifdef _WIN32
using mikudancestudio::test::ShutdownDiagnostic;
using mikudancestudio::test::JapaneseImeSession;
#endif
class InputObserver final : public QObject {
public:
    unsigned preedits = 0, commits = 0;
    QString preedit, committed;
    QJsonArray events;
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::InputMethod) {
            const auto* input = static_cast<QInputMethodEvent*>(event);
            preedit = input->preeditString();
            if (!preedit.isEmpty())
                ++preedits;
            if (!input->commitString().isEmpty()) {
                ++commits;
                committed = input->commitString();
            }
            events.append(QJsonObject{
                {"type", "input"}, {"preedit", preedit}, {"commit", input->commitString()}});
        }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const auto* key = static_cast<QKeyEvent*>(event);
            events.append(QJsonObject{{"type", event->type() == QEvent::KeyPress ? "down" : "up"},
                                      {"key", key->key()},
                                      {"text", key->text()},
                                      {"scan", int(key->nativeScanCode())}});
        }
        return false;
    }
};
}
int main(int argc, char** argv) {
    LifetimeTrace applicationTrace{"application"};
#ifdef _WIN32
    ShutdownDiagnostic shutdownDiagnostic;
#endif
    qInstallMessageHandler(
        [](QtMsgType, const QMessageLogContext& context, const QString& message) {
            std::fprintf(stderr, "%s: %s\n", context.category, message.toUtf8().constData());
            std::fflush(stderr);
        });
#ifdef _WIN32
    if (qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
        qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1") {
        std::fprintf(stderr, "IME constructing QApplication\n");
        std::fflush(stderr);
        shutdownDiagnostic.Arm();
    }
#endif
    QApplication app(argc, argv);
#ifdef _WIN32
    JapaneseImeSession nativeIme;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
#pragma warning(push)
#pragma warning(disable : 4996)
    GetVersionExW(&version);
#pragma warning(pop)
    std::fprintf(stderr, "IME manifest OS compatibility=%lu.%lu build=%lu\n",
                 version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    std::fprintf(stderr, "IME QApplication ready\n");
    std::fflush(stderr);
#endif
    bool platformInput = (QGuiApplication::platformName() == "xcb" &&
                          qEnvironmentVariable("QT_IM_MODULE") == "ibus") ||
                         (QGuiApplication::platformName() == "cocoa" &&
                          qEnvironmentVariable("MDS_SYSTEM_IME") == "kotoeri");
#ifdef _WIN32
    platformInput |= QGuiApplication::platformName() == "windows" &&
                     qEnvironmentVariable("MDS_SYSTEM_IME") == "ms-ime";
#endif
    if (argc != 5 || qEnvironmentVariable("GITHUB_ACTIONS") != "true" ||
        qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") != "1" || !platformInput) {
        std::fprintf(stderr, "Requires isolated CI platform IME: model language status command\n");
        return 2;
    }
    LifetimeTrace sessionTrace{"session"};
    std::fprintf(stderr, "IME constructing generated scene\n");
    std::fflush(stderr);
    editor::EditorSession session;
    std::string reason;
    if (!session.New(reason) || !session.ImportModel(std::filesystem::u8path(argv[1]), reason) ||
        !session.SetEnglishName(0, editor::EnglishNameKind::Model, 0, "before", reason)) {
        std::fprintf(stderr, "%s\n", reason.c_str());
        return 1;
    }
    qt::Language language;
    std::fprintf(stderr, "IME loading UI language\n");
    std::fflush(stderr);
    if (!language.Set(argv[2]))
        return 1;
    LifetimeTrace dialogTrace{"dialog"};
    std::fprintf(stderr, "IME constructing dialog and fonts\n");
    std::fflush(stderr);
    qt::EnglishNameDialog dialog(session, language);
    std::fprintf(stderr, "IME dialog ready\n");
    std::fflush(stderr);
    dialog.setWindowTitle(QString("MDS-IBus-%1").arg(argv[2]));
    QLineEdit* field = nullptr;
    for (auto* edit : dialog.findChildren<QLineEdit*>())
        if (edit->property("mmdControlId").toInt() == 667)
            field = edit;
    if (!field || !dialog.Active())
        return 1;
    LifetimeTrace observerTrace{"observer/timer"};
    InputObserver observer;
    field->installEventFilter(&observer);
    dialog.failed = [&](const std::string& error) { reason = error; };
    const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
    const auto resource = session.Runtime().Resource(0);
    unsigned command = 0;
    unsigned publicationFailures = 0, publicationRetries = 0;
    bool finishing = false;
    bool published = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QFile input(QString::fromLocal8Bit(argv[4]));
        if (input.open(QIODevice::ReadOnly)) {
            const auto request = QJsonDocument::fromJson(input.readAll()).object();
            input.close(); // Do not retain a Windows rename lock while publishing status.
            const auto sequence = unsigned(request["sequence"].toInt());
            if (sequence > command) {
                command = sequence;
                std::fprintf(stderr, "IME command %u: %s\n", command,
                             request["action"].toString().toUtf8().constData());
                std::fflush(stderr);
                if (request["action"] == "select") {
                    if (QGuiApplication::platformName() != "xcb") {
                        dialog.raise();
                        dialog.activateWindow();
                    }
                    field->setFocus();
                    field->selectAll();
#ifdef __APPLE__
                    ActivateCocoaInputWindow(std::uintptr_t(dialog.winId()));
#endif
                }
#ifdef _WIN32
                else if (request["action"] == "activate-ime")
                    reason =
                        nativeIme.Activate(reinterpret_cast<HWND>(dialog.winId())).toStdString();
#endif
#ifdef __APPLE__
                else if (request["action"] == "activate-ime")
                    reason = ActivateCocoaJapanese();
#endif
                else if (request["action"] == "finish")
                    finishing = true;
            }
        }
        format::PmmModelEnglishNames names;
        if (!session.ModelEnglishNames(0, names, reason)) {
            app.exit(1);
            return;
        }
        const bool unchanged = session.Revision() == revision &&
                               session.Runtime().Counters().frames == frames &&
                               session.Runtime().Resource(0) == resource && !session.CanUndo();
        QJsonObject report{
            {"language", language.Code()},
            {"platform", QGuiApplication::platformName()},
            {"qt", qVersion()},
            {"command", int(command)},
            {"finishing", finishing},
            {"publicationRetries", int(publicationRetries)},
            {"focus", field->hasFocus()},
            {"windowActive", dialog.isActiveWindow()},
            {"active", dialog.Active()},
            {"preedit", observer.preedit},
            {"preedits", int(observer.preedits)},
            {"commits", int(observer.commits)},
            {"committed", observer.committed},
            {"events", observer.events},
            {"field", field->text()},
            {"name", QString::fromUtf8(editor::EnglishNameDisplay(names.name).c_str())},
            {"sceneUnchanged", unchanged},
            {"error", QString::fromStdString(reason)}};
#ifdef _WIN32
        if (const auto context = ImmGetContext(reinterpret_cast<HWND>(dialog.winId()))) {
            DWORD conversion = 0, sentence = 0;
            ImmGetConversionStatus(context, &conversion, &sentence);
            report.insert("nativeIme",
                          QJsonObject{{"conversion", int(conversion)},
                                      {"sentence", int(sentence)},
                                      {"open", bool(ImmGetOpenStatus(context))},
                                      {"keyboardLayout",
                                       QString::number(quintptr(GetKeyboardLayout(0)), 16)}});
            ImmReleaseContext(reinterpret_cast<HWND>(dialog.winId()), context);
        }
#endif
        QSaveFile output(QString::fromLocal8Bit(argv[3]));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(QJsonDocument(report).toJson()) < 0) {
            std::fprintf(stderr, "IME status write failed: %s\n",
                         output.errorString().toUtf8().constData());
            std::fflush(stderr);
            app.exit(1);
            return;
        }
        if (!output.commit()) {
            // Python's Windows read handle can briefly deny replacement of the
            // previous snapshot. Retry on the next tick, without blocking IME
            // delivery or quitting before the driver observes the actual error.
            ++publicationRetries;
            if (!publicationFailures++) {
                std::fprintf(stderr, "IME status replacement retry: %s\n",
                             output.errorString().toUtf8().constData());
                std::fflush(stderr);
            }
            if (publicationFailures >= 80)
                app.exit(1);
            return;
        }
        publicationFailures = 0;
        if (!published) {
            published = true;
#ifdef _WIN32
            shutdownDiagnostic.Cancel();
#endif
            std::fprintf(stderr, "IME first status published\n");
            std::fflush(stderr);
        }
        if (finishing) {
            timer.stop();
            const auto result = unchanged && dialog.Active() && reason.empty() ? 0 : 1;
#ifdef _WIN32
            shutdownDiagnostic.Arm();
            const auto releaseError = nativeIme.Finish(reinterpret_cast<HWND>(dialog.winId()));
            if (!releaseError.isEmpty()) {
                std::fprintf(stderr, "%s\n", releaseError.toUtf8().constData());
                app.exit(1);
                return;
            }
#endif
            // Deliver focus/hide notifications before normal window and
            // QApplication destruction. No forced process exit is accepted.
            field->clearFocus();
            dialog.hide();
            std::fprintf(stderr, "IME finish snapshot published; leaving event loop\n");
            std::fflush(stderr);
            QTimer::singleShot(0, &app, [&app, result] { app.exit(result); });
        }
    });
    dialog.show();
    field->setFocus();
    timer.start(25);
    QTimer::singleShot(60000, &app, [&] { app.exit(1); });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        std::fprintf(stderr, "IME application aboutToQuit\n");
        std::fflush(stderr);
    });
    const auto result = app.exec();
    std::fprintf(stderr, "IME event loop returned %d\n", result);
    std::fflush(stderr);
    return result;
}
