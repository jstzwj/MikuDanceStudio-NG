#include "numeric_dialog.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/editor_numeric.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/menu_commands.h"
#include "editor_numeric_fixture.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QInputMethodEvent>
#include <iostream>
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
    CHECK(QGuiApplication::platformName() == "offscreen");
    CHECK(argc == 4);
    QFile file(QString::fromLocal8Bit(argv[3]));
    CHECK(file.open(QIODevice::ReadOnly));
    const auto reference = QJsonDocument::fromJson(file.readAll()).object();
    qt::Language language;
    const std::array<float, 7> initial{1.23456f, -2.34567f, 0, 12.34567f, -45.67891f, 90, 45};
    const QStringList texts{"  1.235", " -2.346", "  0.000", " 12.346",
                            "-45.679", " 90.000", " 45.000"};
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        for (bool camera : {false, true}) {
            qt::NumericDialog dialog(camera, language, initial);
            const auto expected = reference[QString(camera ? "camera." : "bone.") +
                                            (language.Code() == "en" ? "en" : "ja")]
                                      .toObject();
            const auto controls = expected["controls"].toArray();
            const auto rect = expected["rect"].toArray();
            const int unitY = language.Code() == "en" ? 15 : 12;
            CHECK(dialog.size() ==
                  QSize((rect[2].toInt() * 7 + 2) / 4, (rect[3].toInt() * unitY + 4) / 8));
            CHECK(dialog.Template().id == expected["id"].toInt());
            CHECK(dialog.font().pixelSize() == 12);
            if (language.Code() != "zh_CN")
                CHECK(dialog.windowTitle() == expected["title"].toString());
            std::vector<QWidget*> tabOrder;
            for (int i = 0; i < controls.size(); ++i) {
                auto* widget = dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
                CHECK(widget);
                const auto control = controls[i].toObject();
                const auto r = control["rect"].toArray();
                CHECK(widget->geometry() ==
                      QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                            (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
                CHECK(widget->property("mmdControlId").toInt() == control["id"].toInt());
                if (control["kind"] == "Edit") {
                    CHECK(qobject_cast<QLineEdit*>(widget));
                    const unsigned field =
                        control["id"].toInt() == 644 ? 6 : control["id"].toInt() - 637;
                    CHECK(dialog.Field(field) == widget);
                    CHECK(dialog.Field(field)->text() == texts[field]);
                    tabOrder.push_back(widget);
                } else if (control["kind"] == "Button") {
                    auto* button = qobject_cast<QPushButton*>(widget);
                    CHECK(button && button->isDefault() == (control["id"].toInt() == 1));
                    if (language.Code() != "zh_CN")
                        CHECK(button->text() == control["text"].toString());
                    tabOrder.push_back(widget);
                } else {
                    auto* label = qobject_cast<QLabel*>(widget);
                    CHECK(label);
                    if (language.Code() != "zh_CN")
                        CHECK(label->text() == control["text"].toString());
                }
            }
            CHECK(dialog.focusWidget() == dialog.Field(0));
            CHECK(dialog.Field(0)->selectedText() == texts[0]);
            CHECK(bool(dialog.Field(6)) == camera);
            for (std::size_t i = 1; i < tabOrder.size(); ++i) {
                auto* next = tabOrder[i - 1]->nextInFocusChain();
                while (next != tabOrder[i] && next->focusPolicy() == Qt::NoFocus)
                    next = next->nextInFocusChain();
                CHECK(next == tabOrder[i]);
            }
            dialog.Field(0)->setText("123.5");
            dialog.reject();
            CHECK(dialog.Values() == initial && dialog.result() == QDialog::Rejected);
            dialog.Field(1)->setText("nan");
            dialog.accept();
            CHECK(dialog.result() == QDialog::Rejected && dialog.Values() == initial);
            dialog.Field(1)->setText("  +12.25suffix");
            dialog.Field(2)->setText("not a number");
            dialog.Field(3)->setText("100001.5");
            dialog.accept();
            CHECK(dialog.result() == QDialog::Accepted);
            CHECK(dialog.Values()[0] == 123.5f && dialog.Values()[1] == 12.25f);
            CHECK(dialog.Values()[2] == 0 && dialog.Values()[3] == 100001.5f);
            dialog.Field(0)->setText("+-1");
            dialog.accept();
            CHECK(dialog.Values()[0] == 0);
            dialog.Field(0)->setText("1.00000000000000000e9");
            dialog.Field(1)->setText(QString(18, ' ') + "9.5");
            dialog.accept();
            CHECK(dialog.Values()[0] == 1 && dialog.Values()[1] == 9);
            const QLocale previousLocale;
            QLocale::setDefault(QLocale(QLocale::German));
            for (const auto& item : std::vector<std::pair<QString, float>>{{".125rest", .125f},
                                                                           {"-1.25e2suffix", -125},
                                                                           {"+2.5E-1", .25f},
                                                                           {"1e+oops", 1},
                                                                           {"1,25", 1},
                                                                           {"--1", 0},
                                                                           {"0x10", 0}}) {
                dialog.Field(0)->setText(item.first);
                dialog.accept();
                CHECK(dialog.Values()[0] == item.second);
            }
            for (const char* invalid : {"inf", "-infinity", "NaN", "1e99", "1e999", "1e-999"}) {
                const auto before = dialog.Values();
                dialog.setResult(QDialog::Rejected);
                dialog.Field(0)->setText(invalid);
                dialog.accept();
                CHECK(dialog.result() == QDialog::Rejected && dialog.Values() == before);
            }
            QLocale::setDefault(previousLocale);
            CHECK(!dialog.isVisible());
            qt::NumericDialog keyboard(camera, language, initial);
            keyboard.show(); // Guarded above: the offscreen QPA never exposes a desktop window.
            QCoreApplication::processEvents();
            keyboard.Field(0)->selectAll();
            QInputMethodEvent composition(QStringLiteral("12"), {});
            QCoreApplication::sendEvent(keyboard.Field(0), &composition);
            CHECK(keyboard.Values() == initial);
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("12.75"));
            QCoreApplication::sendEvent(keyboard.Field(0), &commit);
            CHECK(keyboard.Field(0)->text() == "12.75");
            QTest::keyClick(keyboard.Field(0), Qt::Key_Return);
            CHECK(keyboard.result() == QDialog::Accepted && keyboard.Values()[0] == 12.75f);
            keyboard.setResult(QDialog::Accepted);
            QTest::keyClick(keyboard.Field(0), Qt::Key_Escape);
            CHECK(keyboard.result() == QDialog::Rejected);
        }
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::ScreenSizeDialog dialog(language, {640, 480}, {2048, 1024});
        const auto expected =
            reference[QString("screen.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        CHECK(dialog.Template().id == expected["id"].toInt());
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            CHECK(widget->geometry() ==
                  QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                        (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
            CHECK(widget->property("mmdControlId").toInt() == control["id"].toInt());
            if (language.Code() != "zh_CN") {
                if (auto* label = qobject_cast<QLabel*>(widget))
                    CHECK(label->text() == control["text"].toString());
                if (auto* button = qobject_cast<QPushButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        CHECK(dialog.Field(0)->text() == "640" && dialog.Field(1)->text() == "480");
        CHECK(dialog.focusWidget() == dialog.Field(0) && dialog.Field(0)->selectedText() == "640");
        unsigned errors = 0;
        bool tooLarge = false;
        dialog.invalid = [&](bool large) {
            ++errors;
            tooLarge = large;
        };
        dialog.Field(0)->setText("2049");
        dialog.accept();
        CHECK(errors == 1 && tooLarge && dialog.Value() == QSize(640, 480));
        dialog.Field(0)->setText(" +1024.75ignored");
        for (const char* invalid : {"0", "-2", "oops", "+-32", "99999999999999999"}) {
            const auto before = errors;
            dialog.Field(1)->setText(invalid);
            dialog.accept();
            CHECK(errors == before + 1 && dialog.Value() == QSize(640, 480));
        }
        dialog.Field(1)->setText("512pixels");
        dialog.reject();
        CHECK(dialog.Value() == QSize(640, 480));
        dialog.accept();
        CHECK(dialog.result() == QDialog::Accepted && dialog.Value() == QSize(1024, 512));
        dialog.Field(0)->setText(QString(18, ' ') + "9.5");
        dialog.accept();
        CHECK(dialog.Value() == QSize(9, 512));
        CHECK(!dialog.isVisible());
    }
    for (unsigned i = 0; i < 256; ++i) {
        const auto input = NumericCameraInput(i), edited = NumericCameraEdited(i);
        NumericHash(hash, editor::CameraNumericValues({input[0], input[1], input[2]},
                                                      {input[3], input[4], input[5]}, input[6]));
        const auto rotation = editor::CameraNumericRotation(edited);
        NumericHash(hash, {edited[0], edited[1], edited[2], rotation[0], rotation[1], rotation[2],
                           -edited[6]});
    }
    std::cout << "Camera numeric corpus hash: " << std::hex << hash << std::dec << '\n';
    CHECK(hash == 0xa78d309a17788cb1ull);
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    auto& session = window.Session();
    const auto root = std::filesystem::absolute(argv[1]);
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.SelectBones({0}, 0, reason));
    window.Refresh();
    const auto command = [&]() -> QAction* {
        for (auto* action : window.findChildren<QAction*>())
            if (action->property("mmdCommandId").toInt() == MDS_CMD_NUMERIC_INPUT)
                return action;
        return nullptr;
    };
    CHECK(command());
    const auto interact = [&](bool camera, bool commit) {
        bool valid = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = dynamic_cast<qt::NumericDialog*>(QApplication::activeModalWidget());
            if (!dialog) {
                QApplication::closeAllWindows();
                return;
            }
            valid = bool(dialog->Field(6)) == camera;
            dialog->Field(0)->setText("8.125");
            dialog->Field(3)->setText("30");
            dialog->Field(4)->setText("-12.5");
            if (camera)
                dialog->Field(6)->setText("67.25");
            if (commit)
                dialog->accept();
            else
                dialog->reject();
        });
        command()->trigger();
        return valid;
    };
    const auto* runtime = &session.Runtime();
    const auto frames = session.Runtime().Counters().frames;
    auto revision = session.Revision();
    CHECK(interact(false, false) && session.Revision() == revision);
    CHECK(interact(false, true));
    CHECK(session.Document().models[0].bonePose[0].components[0] == 8.125f);
    CHECK(session.CanUndo() && session.Undo(reason));
    CHECK(session.Document().models[0].bonePose[0].components[0] == 0);
    CHECK(session.SelectModel(255, reason));
    window.Refresh();
    revision = session.Revision();
    CHECK(interact(true, false) && session.Revision() == revision);
    CHECK(interact(true, true));
    const auto camera = session.Runtime().Session()->Pose().camera;
    CHECK(camera.position[0] == 8.125f && camera.distance == -67.25f);
    CHECK(camera.rotation == editor::CameraNumericRotation({8.125f, 10, 0, 30, -12.5f, 0, 67.25f}));
    CHECK(&session.Runtime() == runtime && session.Runtime().Counters().frames == frames);
    CHECK(!window.isVisible());
    std::cout
        << "Numeric dialogs: binary geometry/labels, tab order, formatting, cancel and editor publication passed\n";
}
