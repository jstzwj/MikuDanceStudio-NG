// Real platform input context qualification. Visible only in an isolated CI display.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <msctf.h>
#include <imm.h>
#include <wrl/client.h>
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
#ifdef _WIN32
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
                                              TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE |
                                                  TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
        if (FAILED(result))
            return QString("TSF activation: 0x%1").arg(unsigned(result), 8, 16, QLatin1Char('0'));
        const auto context = ImmGetContext(window);
        if (!context)
            return "Japanese IME has no native input context";
        const auto opened = ImmSetOpenStatus(context, TRUE);
        const auto converted = ImmSetConversionStatus(
            context, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE | IME_CMODE_ROMAN, IME_SMODE_NONE);
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
    qt::EnglishNameDialog dialog(session, language);
    dialog.setWindowTitle(QString("MDS-IBus-%1").arg(argv[2]));
    QLineEdit* field = nullptr;
    for (auto* edit : dialog.findChildren<QLineEdit*>())
        if (edit->property("mmdControlId").toInt() == 667)
            field = edit;
    if (!field || !dialog.Active())
        return 1;
    InputObserver observer;
    field->installEventFilter(&observer);
    dialog.failed = [&](const std::string& error) { reason = error; };
    const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
    const auto resource = session.Runtime().Resource(0);
    unsigned command = 0;
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
        QSaveFile output(QString::fromLocal8Bit(argv[3]));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(QJsonDocument(report).toJson()) < 0 || !output.commit()) {
            app.exit(1);
            return;
        }
        if (finishing)
            app.exit(unchanged && dialog.Active() && reason.empty() ? 0 : 1);
    });
    dialog.show();
    field->setFocus();
    timer.start(25);
    QTimer::singleShot(60000, &app, [&] { app.exit(1); });
    return app.exec();
}
