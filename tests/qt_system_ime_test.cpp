// Real platform input context qualification. Visible only in an isolated CI display.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <msctf.h>
#include <imm.h>
#include <wrl/client.h>
#include <DbgHelp.h>
#include <thread>
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
class ShutdownDiagnostic {
public:
    ShutdownDiagnostic() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_,
                        THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                        FALSE, 0);
    }
    ~ShutdownDiagnostic() {
        if (done_)
            SetEvent(done_);
        if (worker_.joinable())
            worker_.join();
        if (done_)
            CloseHandle(done_);
        if (main_)
            CloseHandle(main_);
    }
    void Arm() {
        const auto path = qEnvironmentVariable("MDS_IME_DIAGNOSTIC_PATH").toStdWString();
        if (!done_ || path.empty())
            return;
        // Capture only this generated-fixture client, from a separate thread:
        // a blocked IMM/TSF call cannot service a GUI-thread diagnostic timer.
        worker_ = std::thread([this, path] {
            if (WaitForSingleObject(done_, 5000) != WAIT_TIMEOUT)
                return;
            const auto process = GetCurrentProcess();
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
            if (main_ && SymInitialize(process, "", TRUE)) {
                DWORD64 addresses[64]{};
                unsigned count = 0;
                if (SuspendThread(main_) != DWORD(-1)) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(main_, &context)) {
                        STACKFRAME64 frame{};
#ifdef _WIN64
                        constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
                        frame.AddrPC.Offset = context.Rip;
                        frame.AddrStack.Offset = context.Rsp;
                        frame.AddrFrame.Offset = context.Rbp;
#else
                        constexpr DWORD machine = IMAGE_FILE_MACHINE_I386;
                        frame.AddrPC.Offset = context.Eip;
                        frame.AddrStack.Offset = context.Esp;
                        frame.AddrFrame.Offset = context.Ebp;
#endif
                        frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode =
                            AddrModeFlat;
                        while (count < 64 &&
                               StackWalk64(machine, process, main_, &frame, &context, nullptr,
                                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
                               frame.AddrPC.Offset)
                            addresses[count++] = frame.AddrPC.Offset;
                    }
                    ResumeThread(main_);
                }
                for (unsigned index = 0; index < count; ++index) {
                    IMAGEHLP_MODULE64 module{};
                    module.SizeOfStruct = sizeof(module);
                    SymGetModuleInfo64(process, addresses[index], &module);
                    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
                    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen = MAX_SYM_NAME;
                    DWORD64 displacement = 0;
                    SymFromAddr(process, addresses[index], &displacement, symbol);
                    std::fprintf(stderr, "IME shutdown frame %u: %s+0x%llx %s+0x%llx\n", index,
                                 module.ModuleName, addresses[index] - module.BaseOfImage,
                                 symbol->Name, displacement);
                }
                SymCleanup(process);
                std::fflush(stderr);
            }
            const auto output = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output != INVALID_HANDLE_VALUE) {
                const auto written =
                    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), output,
                                      MiniDumpNormal, nullptr, nullptr, nullptr);
                std::fprintf(stderr, "IME shutdown diagnostic dump=%d error=%lu\n", written,
                             written ? 0ul : GetLastError());
                std::fflush(stderr);
                CloseHandle(output);
            }
        });
    }

private:
    HANDLE done_;
    HANDLE main_ = nullptr;
    std::thread worker_;
};
QString ActivateJapaneseIme(HWND window) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ITfInputProcessorProfiles> profiles;
    auto result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&profiles));
    if (FAILED(result))
        return QString("TSF profiles: 0x%1").arg(unsigned(result), 8, 16, QLatin1Char('0'));
    ComPtr<IEnumTfLanguageProfiles> available;
    result = profiles->EnumLanguageProfiles(0x411, &available);
    if (FAILED(result))
        return "Japanese TSF profile enumeration failed";
    TF_LANGUAGEPROFILE profile{};
    ULONG fetched = 0;
    while (available->Next(1, &profile, &fetched) == S_OK && fetched) {
        if (profile.catid != GUID_TFCAT_TIP_KEYBOARD)
            continue;
        ComPtr<ITfInputProcessorProfileMgr> manager;
        result = profiles.As(&manager);
        if (SUCCEEDED(result))
            result = manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x411, profile.clsid,
                                              profile.guidProfile, nullptr,
                                              TF_IPPMF_ENABLEPROFILE |
                                                  TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
        if (FAILED(result))
            return QString("TSF activation: 0x%1").arg(unsigned(result), 8, 16, QLatin1Char('0'));
        const auto context = ImmGetContext(window);
        if (!context)
            return "Japanese IME has no native input context";
        const auto opened = ImmSetOpenStatus(context, TRUE);
        const auto converted = ImmSetConversionStatus(
            context, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE | IME_CMODE_ROMAN,
            IME_SMODE_NONE);
        DWORD conversion = 0, sentence = 0;
        ImmGetConversionStatus(context, &conversion, &sentence);
        std::fprintf(stderr, "Japanese IME conversion=0x%lx sentence=0x%lx open=%d\n", conversion,
                     sentence, ImmGetOpenStatus(context));
        ImmReleaseContext(window, context);
        return opened && converted ? QString() : QString("Cannot select native Hiragana mode");
    }
    return "No installed Japanese TSF keyboard profile";
}
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
    QApplication app(argc, argv);
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
    editor::EditorSession session;
    std::string reason;
    if (!session.New(reason) || !session.ImportModel(std::filesystem::u8path(argv[1]), reason) ||
        !session.SetEnglishName(0, editor::EnglishNameKind::Model, 0, "before", reason)) {
        std::fprintf(stderr, "%s\n", reason.c_str());
        return 1;
    }
    qt::Language language;
    if (!language.Set(argv[2]))
        return 1;
    LifetimeTrace dialogTrace{"dialog"};
    qt::EnglishNameDialog dialog(session, language);
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
                        ActivateJapaneseIme(reinterpret_cast<HWND>(dialog.winId())).toStdString();
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
            report.insert("nativeIme", QJsonObject{
                {"conversion", int(conversion)}, {"sentence", int(sentence)},
                {"open", bool(ImmGetOpenStatus(context))},
                {"keyboardLayout", QString::number(quintptr(GetKeyboardLayout(0)), 16)}});
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
        if (finishing) {
            timer.stop();
            const auto result = unchanged && dialog.Active() && reason.empty() ? 0 : 1;
#ifdef _WIN32
            shutdownDiagnostic.Arm();
#endif
            // Activation belongs to this GUI thread, so it never replaced
            // another app's profile or worker-thread input contexts. Let
            // normal focus/window destruction release this input
            // context instead of synchronously switching layouts during TSF
            // teardown. Deliver the hide/focus notifications before quitting.
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
