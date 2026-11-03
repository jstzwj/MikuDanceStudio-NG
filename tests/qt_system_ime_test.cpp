// Real platform input context qualification. Visible only in an isolated CI display.
#include "english_name_dialog.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include <QApplication>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>

using namespace mikudancestudio;
namespace {
class InputObserver final : public QObject {
public:
    unsigned preedits = 0, commits = 0;
    QString preedit, committed;
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
        }
        return false;
    }
};
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const bool platformInput = (QGuiApplication::platformName() == "xcb" &&
                                qEnvironmentVariable("QT_IM_MODULE") == "ibus") ||
                               (QGuiApplication::platformName() == "cocoa" &&
                                qEnvironmentVariable("MDS_SYSTEM_IME") == "kotoeri");
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
            const auto sequence = unsigned(request["sequence"].toInt());
            if (sequence > command) {
                command = sequence;
                if (request["action"] == "select") {
                    if (QGuiApplication::platformName() == "cocoa") {
                        dialog.raise();
                        dialog.activateWindow();
                    }
                    field->setFocus();
                    field->selectAll();
                } else if (request["action"] == "finish")
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
            {"active", dialog.Active()},
            {"preedit", observer.preedit},
            {"preedits", int(observer.preedits)},
            {"commits", int(observer.commits)},
            {"committed", observer.committed},
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
