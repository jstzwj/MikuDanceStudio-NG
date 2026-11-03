#include "numeric_dialog.hpp"
#include "editor_window.hpp"
#include "viewport.hpp"
#include "mikudancestudio/mmd_dialog_edit_metrics.hpp"
#include "mikudancestudio/gravity_edit.hpp"
#include "mikudancestudio/editor_numeric.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/menu_commands.h"
#include "editor_numeric_fixture.hpp"
#include "gravity_edit_reference.hpp"
#include "editor_morph_scale_fixture.hpp"
#include "editor_morph_purge_fixture.hpp"
#include "editor_camera_scale_fixture.hpp"
#include "editor_bone_scale_fixture.hpp"
#include "editor_physics_key_fixture.hpp"
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
#include <QComboBox>
#include <QWheelEvent>
#include <QColorDialog>
#include <QClipboard>
#include <iostream>
#include <limits>
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
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        bool validScale = false;
        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&validScale);
        CHECK(validScale && std::abs(app.devicePixelRatio() - scale) < 1e-6);
    }

    CHECK(QGuiApplication::platformName() == "offscreen");
    CHECK(argc == 4);
    QFile file(QString::fromLocal8Bit(argv[3]));
    CHECK(file.open(QIODevice::ReadOnly));
    const auto reference = QJsonDocument::fromJson(file.readAll()).object();
    qt::Language language;
    // Match actual no-ES_AUTOHSCROLL EDIT replacement/typing, not just the
    // initial formatted prefill. Programmatic WM_SETTEXT remains unrestricted.
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        const bool english = language.Code() == "en";
        qt::EdgeThicknessDialog input(language, 1);
        auto* field = input.Field();
        for (const char* before : {"", "12.3", "111111111111", "abcdef", "  -4.5"})
            for (const char* inserted : {"123456789", ".25", "x", "", "------"})
                for (unsigned mode = 0; mode < 4; ++mode) {
                    const std::string raw(before), insertion(inserted);
                    const auto initial =
                        raw.substr(0, ui::NumericPrefillLength(raw, english, field->width()));
                    const auto first = mode == 0   ? 0u
                                       : mode == 1 ? initial.size()
                                                   : initial.size() / 2;
                    const auto last = mode == 0 || mode == 3 ? initial.size() : first;
                    const auto count = ui::NumericInsertedLength(initial, first, last, insertion,
                                                                 english, field->width());
                    const auto expected = initial.substr(0, first) + insertion.substr(0, count) +
                                          initial.substr(last);
                    field->setText(QString::fromLatin1(initial.c_str()));
                    field->setSelection(int(first), int(last - first));
                    app.clipboard()->setText(QString::fromLatin1(inserted));
                    QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier);
                    CHECK(field->text().toLatin1().toStdString() == expected);
                }
        field->clear();
        QTest::keyClicks(field, "123456789");
        CHECK(field->text() ==
              QString::fromLatin1("123456789")
                  .left(qsizetype(ui::NumericPrefillLength("123456789", english, field->width()))));
        field->setText("12345678901234567890");
        CHECK(field->text() == "12345678901234567890");
        field->clear();
        QTest::keyClicks(field, "12.3");
        QTest::keyClick(field, Qt::Key_Z, Qt::ControlModifier);
        CHECK(field->text().isEmpty());
    }
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
            QTest::keyClick(keyboard.Field(0), Qt::Key_Return);
            CHECK(keyboard.result() == QDialog::Rejected && keyboard.isVisible() &&
                  keyboard.Values() == initial);
            QTest::keyClick(keyboard.Field(0), Qt::Key_Escape);
            CHECK(keyboard.result() == QDialog::Rejected && keyboard.isVisible() &&
                  keyboard.Values() == initial);
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
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::EdgeThicknessDialog dialog(language, .57f);
        const auto expected =
            reference[QString("edge.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        CHECK(dialog.Template().id == expected["id"].toInt());
        const auto rect = expected["rect"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(dialog.size() ==
              QSize((rect[2].toInt() * 7 + 2) / 4, (rect[3].toInt() * unitY + 4) / 8));
        if (language.Code() != "zh_CN")
            CHECK(dialog.windowTitle() == expected["title"].toString());
        const auto controls = expected["controls"].toArray();
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            CHECK(widget->geometry() ==
                  QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                        (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
            CHECK(widget->property("mmdControlId").toInt() == control["id"].toInt());
            if (auto* label = qobject_cast<QLabel*>(widget)) {
                if (language.Code() != "zh_CN")
                    CHECK(label->text() == control["text"].toString());
            } else if (control["id"].toInt() == 646)
                CHECK(widget == dialog.Field());
            else
                CHECK(widget == dialog.Trackbar());
        }
        CHECK(dialog.Field()->text() == "0.5" && dialog.Trackbar()->value() == 57);
        CHECK(dialog.Trackbar()->minimum() == 0 && dialog.Trackbar()->maximum() == 200);
        CHECK(dialog.Trackbar()->tickPosition() == QSlider::NoTicks);
        CHECK(dialog.focusWidget() == dialog.Field() && dialog.Field()->selectedText() == "0.5");
        unsigned edits = 0;
        float value = .57f;
        dialog.changed = [&](float next) {
            ++edits;
            value = next;
        };
        dialog.show(); // Main's offscreen QPA guard forbids exposing a desktop window.
        QCoreApplication::processEvents();
        dialog.Field()->setText("1.23456789");
        dialog.Trackbar()->setFocus();
        QCoreApplication::processEvents();
        CHECK(edits == 0);
        struct Input {
            const char* text;
            float value;
            int slider;
        };
        for (const auto& item :
             {Input{"1.23456789", 1.23456f, 123}, Input{"-0.25", -.25f, 0},
              Input{"2.50", 2.5f, 200}, Input{"nonsense", 0, 0}, Input{"0.57", .57f, 57},
              Input{"2e30", 2e30f, 0}, Input{"1,25", 1, 100}}) {
            dialog.Field()->setText(item.text);
            const auto before = edits;
            QTest::keyClick(dialog.Field(), Qt::Key_Return);
            CHECK(edits == before + 1 && value == item.value);
            CHECK(dialog.Trackbar()->value() == item.slider);
            CHECK(dialog.Field()->text() == item.text && dialog.isVisible());
        }
        for (const char* text : {"inf", "nan", "1e99"}) {
            const auto before = edits;
            dialog.Field()->setText(text);
            QTest::keyClick(dialog.Field(), Qt::Key_Return);
            CHECK(edits == before && dialog.Field()->selectedText() == text);
        }
        dialog.Trackbar()->setValue(57);
        CHECK(value == .57f && dialog.Field()->text() == "0.5");
        for (int position = 0; position <= 200; ++position) {
            dialog.Trackbar()->setValue(position);
            CHECK(value == float(position) / 100.f);
            CHECK(dialog.Field()->text() == QString::asprintf("%3.2f", double(value)).left(3));
        }
        dialog.Trackbar()->setValue(57);
        const auto before = edits;
        dialog.Field()->setText("1.75");
        dialog.close();
        CHECK(edits == before && value == .57f && !dialog.isVisible());

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        auto& editor = owner.Session();
        CHECK(owner.SetLanguage(code));
        CHECK(editor.ImportModel(root / "pose.pmd", reason));
        CHECK(editor.ImportModel(root / "articulated.pmx", reason));
        CHECK(editor.SelectModel(0, reason));
        CHECK(editor.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        CHECK(editor.Undo(reason) && editor.CanRedo());
        CHECK(owner.Save(root / "edge.pmm") && !editor.Dirty());
        owner.Refresh();
        const auto action = [&](int id) -> QAction* {
            for (auto* candidate : owner.findChildren<QAction*>())
                if (candidate->property("mmdCommandId").toInt() == id)
                    return candidate;
            return nullptr;
        };
        const auto findDialog = [&]() -> qt::EdgeThicknessDialog* {
            for (auto* child : owner.findChildren<QDialog*>())
                if (auto* result = dynamic_cast<qt::EdgeThicknessDialog*>(child))
                    return result;
            return nullptr;
        };
        CHECK(action(MDS_CMD_EDGE_THICKNESS)->isEnabled());
        action(MDS_CMD_EDGE_THICKNESS)->trigger();
        QPointer<qt::EdgeThicknessDialog> live = findDialog();
        CHECK(live && !live->isVisible());
        live->Field()->setText("1.75pending");
        action(MDS_CMD_EDGE_THICKNESS)->trigger();
        CHECK(findDialog() == live && live->Field()->text() == "1.75pending");
        const auto frames = editor.Runtime().Counters().frames;
        const auto maxFrame = editor.Document().timeline.lastRegisteredFrame;
        const auto* runtime = &editor.Runtime();
        live->Trackbar()->setValue(57);
        CHECK(editor.Document().models[0].edgeScale == .57f && editor.Dirty());
        CHECK(editor.Runtime().Counters().frames == frames);
        CHECK(editor.CanRedo() && !editor.CanUndo());
        CHECK(editor.Redo(reason) && editor.Document().models[0].edgeScale == .57f);
        CHECK(editor.Undo(reason) && editor.Document().models[0].edgeScale == .57f);
        CHECK(owner.Save(root / "edge.pmm") && !editor.Dirty());
        live->Field()->setText("0.57"); // WM_SETTEXT can bypass the native insertion width limit.
        QTest::keyClick(live->Field(), Qt::Key_Return);
        CHECK(editor.Dirty()); // Even an accepted unchanged value sets the native modified flag.
        auto* modelCombo = static_cast<QComboBox*>(owner.Control(panel::kMainComboModel));
        const auto selectModel = [&](unsigned slot) {
            const int index = modelCombo->findData(slot);
            modelCombo->setCurrentIndex(index);
            QMetaObject::invokeMethod(modelCombo, "activated", Qt::DirectConnection,
                                      Q_ARG(int, index));
        };
        live->Field()->setText("uncommitted");
        selectModel(1);
        CHECK(editor.Selected().model == 1 && live && live->Field()->text() == "1.0" &&
              live->Trackbar()->value() == 100);
        live->Field()->setText("uncommitted");
        selectModel(1);
        CHECK(live->Field()->text() == "1.0" && live->Trackbar()->value() == 100);
        live->Field()->setText("2.50");
        QTest::keyClick(live->Field(), Qt::Key_Return);
        CHECK(editor.Document().models[0].edgeScale == .57f &&
              editor.Document().models[1].edgeScale == 2.5f);
        live->Field()->setText("uncommitted");
        selectModel(255);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(live.isNull());
        CHECK(!action(MDS_CMD_EDGE_THICKNESS)->isEnabled());
        const auto revision = editor.Revision();
        CHECK(editor.SetEdgeThickness(.17f, reason));
        CHECK(editor.Revision() == revision && editor.Document().models[1].edgeScale == 2.5f);
        selectModel(0);
        action(MDS_CMD_EDGE_THICKNESS)->trigger();
        live = findDialog();
        CHECK(live && live->Field()->text() == "0.5" && live->Trackbar()->value() == 57);
        CHECK(editor.Play(reason));
        owner.Refresh();
        const auto epoch = editor.PlaybackEpoch();
        const auto cursor = editor.CursorSeconds();
        const auto playbackFrames = runtime->Counters().frames;
        live->Field()->setText("-0.25");
        QTest::keyClick(live->Field(), Qt::Key_Return);
        CHECK(editor.Playing() && editor.PlaybackEpoch() == epoch &&
              editor.CursorSeconds() == cursor);
        CHECK(editor.Document().models[0].edgeScale == -.25f &&
              runtime->Counters().frames == playbackFrames);
        editor.Pause();
        CHECK(&editor.Runtime() == runtime &&
              editor.Document().timeline.lastRegisteredFrame == maxFrame);
        CHECK(owner.Save(root / "edge.pmm"));
        CHECK(editor.UnloadModel(0, reason));
        CHECK(editor.SelectModel(255, reason));
        const auto unloadedRevision = editor.Revision();
        live->Trackbar()->setValue(99);
        CHECK(editor.Revision() == unloadedRevision);
        live->Field()->setText("1.99");
        live->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(live.isNull());
        CHECK(owner.Open(root / "edge.pmm"));
        CHECK(editor.Document().models[0].edgeScale == -.25f &&
              editor.Document().models[1].edgeScale == 2.5f);
        CHECK(editor.SelectModel(1, reason));
        owner.Refresh();
        action(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
        action(MDS_CMD_EDGE_THICKNESS)->trigger();
        live = findDialog();
        CHECK(live && live->Field()->text() == "2.5" &&
              live->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        CHECK(owner.Save(root / "edge.pmm"));
        live->Field()->setText("pending reopen");
        CHECK(owner.Open(root / "edge.pmm"));
        CHECK(live && live->Field()->text() == "2.5" && live->Trackbar()->value() == 200);
        const auto savedRevision = editor.Revision();
        CHECK(!editor.SetEdgeThickness(std::numeric_limits<float>::infinity(), reason));
        CHECK(editor.Revision() == savedRevision && !editor.Dirty());
        CHECK(!owner.isVisible() && !owner.SeparateWindow()->isVisible() && !live->isVisible());
        live->Field()->setText("1.99");
        CHECK(owner.close());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(live.isNull() && editor.Document().models[1].edgeScale == 2.5f);
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        auto& editor = owner.Session();
        CHECK(owner.SetLanguage(code) && editor.ImportModel(root / "pose.pmd", reason));
        CHECK(owner.Save(root / "outline-dialog.pmm"));
        QAction* action = nullptr;
        for (auto* candidate : owner.findChildren<QAction*>())
            if (candidate->property("mmdCommandId").toInt() == MDS_CMD_EDGE_COLOR)
                action = candidate;
        CHECK(action && action->isEnabled());
        auto* viewport = owner.RenderWindow();
        CHECK(viewport->PrepareFrame(128, 96, reason));
        const auto builds = viewport->FrameBuilds(), frames = editor.Runtime().Counters().frames;
        const auto revision = editor.Revision();
        for (bool accept : {false, true}) {
            bool visited = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                visited = dialog->currentColor() == QColor(0, 0, 0);
                dialog->setCurrentColor(QColor(17, 83, 149));
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(visited && !editor.Dirty() && editor.Revision() == revision);
            CHECK(editor.Document().models[0].pmdEdgeDiffuse ==
                  (accept ? 0xff115395u : 0xff000000u));
        }
        CHECK(viewport->PrepareFrame(128, 96, reason) && viewport->FrameBuilds() == builds + 1);
        CHECK(editor.Runtime().Counters().frames == frames && !owner.isVisible());
        CHECK(owner.Save(root / "outline-dialog.pmm") && owner.Open(root / "outline-dialog.pmm"));
        CHECK(editor.Document().models[0].pmdEdgeDiffuse == 0xff115395u);
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::CenterBiasDialog dialog(language);
        const auto expected =
            reference[QString("center_bias.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        CHECK(dialog.Template().id == expected["id"].toInt());
        const auto rect = expected["rect"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(dialog.size() ==
              QSize((rect[2].toInt() * 7 + 2) / 4, (rect[3].toInt() * unitY + 4) / 8));
        if (language.Code() != "zh_CN")
            CHECK(dialog.windowTitle() == expected["title"].toString());
        const auto controls = expected["controls"].toArray();
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
        CHECK(dialog.focusWidget() == dialog.Field(0) && dialog.Field(0)->selectedText() == "0.0");
        CHECK(dialog.Field(0)->nextInFocusChain() == dialog.Field(1) &&
              dialog.Field(1)->nextInFocusChain() == dialog.Field(2));
        dialog.Field(0)->setText("1.23456789012345678e9");
        dialog.Field(1)->setText("-.25suffix");
        dialog.Field(2)->setText("nonsense");
        dialog.reject();
        CHECK(dialog.Values() == (std::array<float, 3>{}));
        dialog.accept();
        CHECK(dialog.result() == QDialog::Accepted && dialog.Values()[0] == 1.2345679f &&
              dialog.Values()[1] == -.25f && dialog.Values()[2] == 0);
        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(owner.SetLanguage(code));
        CHECK(BoneScaleFixture(owner.Session(), root, "facial.pmd", {0, 0, 0, 1}, reason));
        owner.Refresh();
        QAction* action = nullptr;
        for (auto* candidate : owner.findChildren<QAction*>())
            if (candidate->property("mmdCommandId").toInt() == MDS_CMD_CENTER_POSITION_BIAS)
                action = candidate;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        const auto frames = owner.Session().Runtime().Counters().frames;
        bool visited = false;
        for (bool accept : {false, true}) {
            QTimer::singleShot(0, &owner, [&] {
                auto* form = dynamic_cast<qt::CenterBiasDialog*>(QApplication::activeModalWidget());
                if (!form) {
                    QApplication::closeAllWindows();
                    return;
                }
                visited = true;
                form->Field(0)->setText("1.25");
                form->Field(1)->setText("-2.75");
                if (accept)
                    form->accept();
                else
                    form->reject();
            });
            action->trigger();
            CHECK(visited && (owner.Session().Revision() != revision) == accept);
        }
        CHECK(owner.Session().Document().models[0].boneKeys.dense[0].position[0] == 2.5f &&
              owner.Session().Document().models[0].boneKeys.dense[0].position[1] == -5.25f);
        CHECK(owner.Session().Runtime().Counters().frames == frames && owner.Session().CanUndo());
        CHECK(owner.Session().Undo(reason) && owner.Session().Redo(reason));
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        owner.Session().Pause();
        CHECK(owner.Session().SelectModel(255, reason));
        owner.Refresh();
        CHECK(!action->isEnabled() && !owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::GroundShadowDialog dialog(language, .57f);
        const auto expected =
            reference[QString("ground_shadow.") + (language.Code() == "en" ? "en" : "ja")]
                .toObject();
        CHECK(dialog.Template().id == expected["id"].toInt());
        const auto rect = expected["rect"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(dialog.size() ==
              QSize((rect[2].toInt() * 7 + 2) / 4, (rect[3].toInt() * unitY + 4) / 8));
        if (language.Code() != "zh_CN")
            CHECK(dialog.windowTitle() == expected["title"].toString());
        const auto controls = expected["controls"].toArray();
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            CHECK(widget->geometry() ==
                  QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                        (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
            CHECK(widget->property("mmdControlId").toInt() == control["id"].toInt());
            if (auto* label = qobject_cast<QLabel*>(widget)) {
                if (language.Code() != "zh_CN")
                    CHECK(label->text() == control["text"].toString());
            } else
                CHECK(widget == (control["id"].toInt() == 626
                                     ? static_cast<QWidget*>(dialog.Field())
                                     : static_cast<QWidget*>(dialog.Trackbar())));
        }
        const auto prefill = [&](float value) {
            const auto formatted = QString::asprintf("%3.2f", double(value));
            const auto bytes = formatted.toLatin1();
            return formatted.left(qsizetype(ui::NumericPrefillLength(
                std::string_view(bytes.constData(), std::size_t(bytes.size())),
                language.Code() == "en", dialog.Field()->width())));
        };
        CHECK(dialog.Field()->text() == prefill(.57f) && dialog.Trackbar()->value() == 57);
        CHECK(dialog.Trackbar()->minimum() == 0 && dialog.Trackbar()->maximum() == 200 &&
              dialog.Trackbar()->tickInterval() == 100 &&
              dialog.Trackbar()->tickPosition() == QSlider::TicksBelow);
        unsigned edits = 0;
        float value = .57f;
        dialog.changed = [&](float next) {
            ++edits;
            value = next;
        };
        dialog.show(); // Guarded offscreen QPA, never a desktop window.
        QCoreApplication::processEvents();
        dialog.Field()->setText("1.75");
        dialog.Trackbar()->setFocus();
        QCoreApplication::processEvents();
        CHECK(edits == 0);
        struct Input {
            const char* text;
            float value;
        };
        for (const auto& input :
             {Input{"1.23456789", 1.23456f}, Input{"-0.25", -.25f}, Input{"2.50", 2.5f},
              Input{"nonsense", 0}, Input{"0.57", .57f}, Input{"2e30", 2e30f}, Input{"1,25", 1}}) {
            dialog.Field()->setText(input.text);
            const auto before = edits;
            QTest::keyClick(dialog.Field(), Qt::Key_Return);
            CHECK(edits == before + 1 && value == input.value && dialog.Trackbar()->value() == 57);
            CHECK(dialog.Field()->text() == input.text && dialog.isVisible());
        }
        for (int position = 0; position <= 200; ++position) {
            dialog.Trackbar()->setValue(position);
            CHECK(value == float(position) / 100.f && dialog.Field()->text() == prefill(value));
        }
        dialog.Field()->setText("pending");
        const auto before = edits;
        dialog.close();
        CHECK(edits == before && value == 2.f);
        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        auto& editor = owner.Session();
        CHECK(owner.SetLanguage(code) && editor.ImportModel(root / "pose.pmd", reason));
        CHECK(editor.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        CHECK(editor.Undo(reason) && editor.CanRedo());
        CHECK(owner.Save(root / "ground-shadow.pmm"));
        owner.Refresh();
        QAction* action = nullptr;
        for (auto* candidate : owner.findChildren<QAction*>())
            if (candidate->property("mmdCommandId").toInt() == MDS_CMD_GROUND_SHADOW_COLOR)
                action = candidate;
        CHECK(action && action->isEnabled());
        action->trigger();
        QPointer<qt::GroundShadowDialog> live;
        for (auto* child : owner.findChildren<QDialog*>())
            if (auto* ground = dynamic_cast<qt::GroundShadowDialog*>(child))
                live = ground;
        CHECK(live && !live->isVisible());
        auto* viewport = owner.RenderWindow();
        CHECK(viewport->PrepareFrame(128, 96, reason));
        const auto builds = viewport->FrameBuilds();
        const auto revision = editor.Revision(), updates = editor.Runtime().Counters().frames;
        const auto* runtime = &editor.Runtime();
        live->Trackbar()->setValue(57);
        CHECK(editor.Document().physics.projectedShadowAmbient == .57f && !editor.Dirty());
        CHECK(editor.Revision() == revision && runtime->Counters().frames == updates &&
              !editor.CanUndo() && editor.CanRedo());
        CHECK(viewport->PrepareFrame(128, 96, reason) && viewport->FrameBuilds() == builds + 1);
        CHECK(viewport->PrepareFrame(128, 96, reason) && viewport->FrameBuilds() == builds + 1);
        CHECK(editor.Redo(reason) && editor.Document().physics.projectedShadowAmbient == .57f);
        CHECK(editor.Undo(reason) && editor.Document().physics.projectedShadowAmbient == .57f);
        CHECK(!editor.SetGroundShadowColor(std::numeric_limits<float>::infinity()));
        CHECK(editor.Document().physics.projectedShadowAmbient == .57f);
        CHECK(owner.Save(root / "ground-shadow.pmm"));
        live->Field()->setText("1.75pending");
        action->trigger();
        CHECK(live->Field()->text() == "1.75pending");
        CHECK(editor.SetGroundShadowColor(-.25f));
        CHECK(owner.Open(root / "ground-shadow.pmm"));
        CHECK(editor.Document().physics.projectedShadowAmbient == .57f && live &&
              live->Field()->text() == "1.75pending" && live->Trackbar()->value() == 57);
        CHECK(editor.Play(reason));
        owner.Refresh();
        const auto epoch = editor.PlaybackEpoch(), frames = editor.Runtime().Counters().frames;
        QTest::keyClick(live->Field(), Qt::Key_Return);
        CHECK(editor.Playing() && editor.PlaybackEpoch() == epoch &&
              editor.Runtime().Counters().frames == frames &&
              editor.Document().physics.projectedShadowAmbient == 1.75f && !editor.Dirty());
        editor.Pause();
        CHECK(editor.New(reason));
        owner.Refresh();
        CHECK(editor.Document().physics.projectedShadowAmbient == 1.75f && live &&
              live->Field()->text() == "1.75pending" && live->Trackbar()->value() == 57);
        CHECK(!owner.isVisible() && !live->isVisible());
        live->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(live.isNull() && editor.Document().physics.projectedShadowAmbient == 1.75f);
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        format::PmmEvaluatedGravity value{0, 3, 9.8f, {0, -1, 0}};
        qt::GravityDialog form(language, value);
        const auto expected =
            reference[QString("gravity.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.Template().id == expected["id"].toInt() && controls.size() == 16);
        CHECK(form.size() == QSize(280, (102 * unitY + 4) / 8));
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
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
                if (auto* button = qobject_cast<QAbstractButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        CHECK(!form.Field(4)->isEnabled());
        CHECK(form.Trackbar(0)->pageStep() == 40 && form.Trackbar(0)->singleStep() == 1);
        for (const auto& key : std::vector<std::pair<Qt::Key, int>>{{Qt::Key_Left, -1},
                                                                    {Qt::Key_Up, -1},
                                                                    {Qt::Key_Right, 1},
                                                                    {Qt::Key_Down, 1},
                                                                    {Qt::Key_PageUp, -40},
                                                                    {Qt::Key_PageDown, 40},
                                                                    {Qt::Key_Home, -100},
                                                                    {Qt::Key_End, 100}}) {
            form.Trackbar(0)->setValue(0);
            QTest::keyClick(form.Trackbar(0), key.first);
            CHECK(form.Trackbar(0)->value() == key.second);
        }
        unsigned edits = 0, registrations = 0;
        form.changed = [&](unsigned channel, float number) {
            if (editor::EditGravityChannel(value, channel, number))
                ++edits;
        };
        form.registered = [&] { ++registrations; };
        for (int position = -100; position <= 100; ++position) {
            form.Trackbar(0)->setValue(position);
            CHECK(value.direction[0] == float(double(position) / 100.));
        }
        form.Field(0)->setText("123456789.25tail");
        CHECK(value.acceleration == 123456789.25f && form.Field(0)->text() == "123456789.25tail");
        const auto beforeEnter = edits;
        QTest::keyClick(form.Field(0), Qt::Key_Return);
        CHECK(edits == beforeEnter + 1 && registrations == 0);
        form.Field(1)->setText("2.75");
        CHECK(value.direction[0] == 2.75f && form.Trackbar(0)->value() == 100);
        form.Field(4)->setText("-3.9noise");
        CHECK(value.noise == -3);
        form.Field(4)->setText("2147483648");
        CHECK(value.noise == std::numeric_limits<std::int32_t>::min());
        form.accept();
        CHECK(registrations == 1);
        form.Trackbar(0)->setValue(0);
        for (unsigned i = 0; i < kDialogWheelDeltas.size(); ++i) {
            QWheelEvent event(QPointF(10, 5), QPointF(10, 5), {}, QPoint(0, kDialogWheelDeltas[i]),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(form.Trackbar(0), &event);
            CHECK(form.Trackbar(0)->value() == kDialogWheelPositions[i]);
        }
        for (const auto& sample : kGravityMirrorReference) {
            value.acceleration = sample.input;
            form.Mirror([&] { return value; });
            CHECK(form.Field(0)->text() == sample.text && value.acceleration == sample.output);
        }

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(owner.SetLanguage(code));
        auto& editor = owner.Session();
        CHECK(editor.ImportModel(root / "articulated.pmx", reason));
        CHECK(editor.ImportAccessory(root / "bound.x", reason));
        CHECK(editor.SetBone(0, 0, {1, 0, 0}, {0, 0, 0, 1}, reason));
        CHECK(editor.SetBone(0, 0, {2, 0, 0}, {0, 0, 0, 1}, reason));
        CHECK(editor.Undo(reason));
        CHECK(owner.Save(root / "gravity.pmm"));
        owner.Refresh();
        QAction* action = nullptr;
        for (auto* entry : owner.findChildren<QAction*>())
            if (entry->property("mmdCommandId").toInt() == MDS_CMD_GRAVITY_SETTINGS)
                action = entry;
        CHECK(action && action->isEnabled());
        action->trigger();
        QPointer<qt::GravityDialog> live;
        for (auto* dialog : owner.findChildren<QDialog*>())
            if (auto* gravity = dynamic_cast<qt::GravityDialog*>(dialog))
                live = gravity;
        CHECK(live && !live->isVisible() && !owner.isVisible());
        action->trigger();
        CHECK(owner.findChildren<QDialog*>().size() == 1);
        const auto revision = editor.Revision(), frames = editor.Runtime().Counters().frames;
        const auto updates = editor.Runtime().Counters().editorUpdates;
        const bool undo = editor.CanUndo(), redo = editor.CanRedo();
        live->Field(0)->setText(".375preview");
        QTest::keyClick(live->Field(0), Qt::Key_Return);
        CHECK(editor.Document().physics.gravityMagnitude == .375f && !editor.Dirty());
        CHECK(editor.Document().gravityKeys.sparse.empty());
        live->Field(1)->setText("0");
        live->Field(2)->setText("0");
        live->Field(3)->setText("0");
        CHECK(editor.Document().physics.gravityDirection[1] == .1f);
        live->Noise()->click();
        CHECK(editor.Document().physics.gravityNoiseEnabled && live->Field(4)->isEnabled());
        live->Field(4)->setText("-3.9");
        CHECK(editor.Document().physics.gravityNoise == -3);
        live->Trackbar(0)->setValue(57);
        CHECK(editor.Document().physics.gravityDirection[0] == .57f);
        CHECK(editor.Revision() == revision && editor.Runtime().Counters().frames == frames &&
              editor.Runtime().Counters().editorUpdates == updates);
        CHECK(editor.CanUndo() == undo && editor.CanRedo() == redo);
        live->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(live.isNull() && editor.Document().physics.gravityMagnitude == 9.8f);
        CHECK(editor.SelectModel(255, reason) && editor.Seek(10, reason));
        owner.Refresh();
        action->trigger();
        for (auto* dialog : owner.findChildren<QDialog*>())
            if (auto* gravity = dynamic_cast<qt::GravityDialog*>(dialog))
                live = gravity;
        CHECK(live);
        live->Field(0)->setText("3.125");
        CHECK(editor.SetSelectedKeys(
            {{editor::Track::Camera, 255, 0, 0}, {editor::Track::Accessory, 0, 0, 0}}, reason));
        live->accept();
        CHECK(live && editor.Dirty() && editor.Document().gravityKeys.sparse.size() == 1);
        CHECK(editor.Document().gravityKeys.sparse[0].value.acceleration == 3.125f);
        CHECK(!editor.Document().cameraKeys.dense[0].selected &&
              !editor.Document().accessories[0].keyRecords.dense[0].selected);
        CHECK(editor.Play(reason));
        owner.Refresh();
        live->Field(0)->setText("1.5");
        const auto playFrames = editor.Runtime().Counters().frames;
        live->accept();
        CHECK(editor.Playing() && editor.Runtime().Playing() &&
              editor.Runtime().Session()->Playing());
        CHECK(editor.Runtime().Counters().frames == playFrames);
        editor.Pause();
        CHECK(editor.Seek(5, reason));
        CHECK(editor.Document().physics.gravityMagnitude == 2.3125f);
        const auto seekFrames = editor.Runtime().Counters().frames;
        owner.Refresh();
        CHECK(live->Field(0)->text() == "2.31" &&
              editor.Document().physics.gravityMagnitude == 2.31f);
        CHECK(editor.Runtime().Counters().frames == seekFrames);
        live->Field(0)->setText("9.25pending");
        CHECK(editor.Seek(5, reason)); // same-frame track refresh still replaces pending text
        owner.Refresh();
        CHECK(live->Field(0)->text() == "2.31");
        live->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(live.isNull() && editor.Document().physics.gravityMagnitude == 2.3125f);
        CHECK(owner.Save(root / "gravity.pmm") && owner.Open(root / "gravity.pmm"));
        CHECK(editor.Document().gravityKeys.dense[0].acceleration == 1.5f &&
              editor.Document().gravityKeys.sparse[0].value.acceleration == 3.125f);
        CHECK(editor.SelectModel(0, reason));
        CHECK(!editor.CanUndo()); // Opening intentionally starts fresh history.
        owner.Refresh();
        for (auto* entry : owner.findChildren<QAction*>())
            if (entry->property("mmdCommandId").toInt() == MDS_CMD_GRAVITY_SETTINGS)
                action = entry;
        action->trigger();
        CHECK(owner.close());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(owner.findChildren<QDialog*>().empty() && owner.LastError().isEmpty());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::MorphScaleDialog form(language);
        const auto expected =
            reference[QString("morph_scale.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.Template().id == expected["id"].toInt() &&
              form.size() == QSize(210, (45 * unitY + 4) / 8));
        for (int i = 0; i < controls.size(); ++i) {
            const auto control = controls[i].toObject();
            const auto rect = control["rect"].toArray();
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget && widget->geometry() == QRect((rect[0].toInt() * 7 + 2) / 4,
                                                        (rect[1].toInt() * unitY + 4) / 8,
                                                        (rect[2].toInt() * 7 + 2) / 4,
                                                        (rect[3].toInt() * unitY + 4) / 8));
            if (language.Code() != "zh_CN") {
                if (auto* label = qobject_cast<QLabel*>(widget))
                    CHECK(label->text() == control["text"].toString());
                if (auto* button = qobject_cast<QPushButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        CHECK(form.Field(0)->text() == "1.0" && form.Field(1)->text() == "0.0");
        CHECK(form.focusWidget() == form.Field(0) && form.Field(0)->selectedText() == "1.0");
        form.Field(0)->setText("-2.5suffix");
        form.Field(1)->setText("nan");
        form.accept();
        CHECK(form.result() == QDialog::Rejected && form.Values() == (std::array<float, 2>{1, 0}));
        form.Field(1)->setText(QString(18, ' ') + "9.5");
        form.accept();
        CHECK(form.Values() == (std::array<float, 2>{-2.5f, 9}));
        form.Field(0)->setText("1.00000000000000000e9");
        form.Field(1)->setText("not a number");
        form.accept();
        CHECK(form.Values() == (std::array<float, 2>{1, 0}));

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(MorphScaleFixture(owner.Session(), root, "facial.pmd", reason));
        CHECK(owner.SetLanguage(code));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_MULTIPLY_MORPH_FRAMES)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog =
                    dynamic_cast<qt::MorphScaleDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->Field(0)->text() == "1.0" && dialog->Field(1)->text() == "0.0";
                dialog->Field(0)->setText("2.0");
                dialog->Field(1)->setText("0.5");
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty());
            if (!accept)
                CHECK(owner.Session().Revision() == revision);
            else {
                CHECK(owner.Session().Revision() != revision);
                CHECK(owner.Session().Document().models[0].morphKeys.sparse[0].value.value == 2.f);
                CHECK(owner.Session().Document().models[0].morphKeys.unused.at(19999).value ==
                      1.75f);
            }
        }
        CHECK(!owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::CameraScaleDialog form(language);
        const auto expected =
            reference[QString("camera_scale.") + (language.Code() == "en" ? "en" : "ja")]
                .toObject();
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.Template().id == expected["id"].toInt() &&
              form.size() == QSize(210, (190 * unitY + 4) / 8));
        if (language.Code() != "zh_CN")
            CHECK(form.windowTitle() == expected["title"].toString());
        std::vector<QWidget*> tabOrder;
        for (int i = 0; i < controls.size(); ++i) {
            const auto control = controls[i].toObject();
            const auto rect = control["rect"].toArray();
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget && widget->geometry() == QRect((rect[0].toInt() * 7 + 2) / 4,
                                                        (rect[1].toInt() * unitY + 4) / 8,
                                                        (rect[2].toInt() * 7 + 2) / 4,
                                                        (rect[3].toInt() * unitY + 4) / 8));
            if (widget->focusPolicy() != Qt::NoFocus)
                tabOrder.push_back(widget);
            if (language.Code() != "zh_CN") {
                if (auto* label = qobject_cast<QLabel*>(widget))
                    CHECK(label->text() == control["text"].toString());
                if (auto* button = qobject_cast<QPushButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        CHECK(tabOrder.size() == 18);
        for (std::size_t i = 1; i < tabOrder.size(); ++i) {
            auto* next = tabOrder[i - 1]->nextInFocusChain();
            while (next != tabOrder[i] && next->focusPolicy() == Qt::NoFocus)
                next = next->nextInFocusChain();
            CHECK(next == tabOrder[i]);
        }
        CHECK(form.focusWidget() == form.Field(0) && form.Field(0)->selectedText() == "1.0");
        for (unsigned i = 0; i < 16; ++i)
            CHECK(form.Field(i)->text() == (i % 2 ? "0.0" : "1.0"));
        form.Field(0)->setText("-2.5suffix");
        form.Field(15)->setText("nan");
        form.accept();
        CHECK(form.result() == QDialog::Rejected && form.Values() == CameraScaleIdentity &&
              form.focusWidget() == form.Field(15));
        form.Field(15)->setText(QString(18, ' ') + "9.5");
        form.Field(14)->setText("1.00000000000000000e9");
        form.accept();
        CHECK(form.Values()[0] == -2.5f && form.Values()[14] == 1 && form.Values()[15] == 9);

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(CameraScaleFixture(owner.Session(), root, "facial.pmd", reason));
        CHECK(owner.SetLanguage(code));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_MULTIPLY_CAMERA_FRAMES)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog =
                    dynamic_cast<qt::CameraScaleDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->Values() == CameraScaleIdentity;
                dialog->Field(0)->setText("2.0");
                dialog->Field(1)->setText("0.5");
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty());
            if (!accept)
                CHECK(owner.Session().Revision() == revision);
            else {
                CHECK(owner.Session().Revision() != revision);
                CHECK(owner.Session().Document().cameraKeys.sparse[0].value.eye[0] == 6.5f &&
                      owner.Session().Document().cameraKeys.sparse[2].value.eye[0] == 3.f &&
                      owner.Session().Document().cameraKeys.unused.at(9999).eye[0] == 6.5f);
            }
        }
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        owner.Session().Pause();
        CHECK(owner.Session().SelectModel(0, reason));
        owner.Refresh();
        CHECK(!action->isEnabled() && !owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::BoneScaleDialog form(language);
        const auto expected =
            reference[QString("bone_scale.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.Template().id == expected["id"].toInt() &&
              form.size() == QSize(210, (150 * unitY + 4) / 8));
        if (language.Code() != "zh_CN")
            CHECK(form.windowTitle() == expected["title"].toString());
        std::vector<QWidget*> tabs;
        for (int i = 0; i < controls.size(); ++i) {
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget && widget->geometry() ==
                                QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                                      (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
            if (widget->focusPolicy() != Qt::NoFocus)
                tabs.push_back(widget);
            if (language.Code() != "zh_CN") {
                if (auto* label = qobject_cast<QLabel*>(widget))
                    CHECK(label->text() == control["text"].toString());
                if (auto* button = qobject_cast<QPushButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        CHECK(tabs.size() == 14 && form.focusWidget() == form.Field(0) &&
              form.Field(0)->selectedText() == "1.0");
        for (std::size_t i = 1; i < tabs.size(); ++i) {
            auto* next = tabs[i - 1]->nextInFocusChain();
            while (next != tabs[i] && next->focusPolicy() == Qt::NoFocus)
                next = next->nextInFocusChain();
            CHECK(next == tabs[i]);
        }
        for (unsigned i = 0; i < 12; ++i)
            CHECK(form.Field(i)->text() == (i % 2 ? "0.0" : "1.0"));
        form.Field(0)->setText("1.00000000000000000e9");
        form.Field(11)->setText("inf");
        form.accept();
        CHECK(form.result() == QDialog::Rejected && form.Values() == BoneScaleIdentity &&
              form.focusWidget() == form.Field(11));
        form.Field(11)->setText(QString(18, ' ') + "9.5");
        form.accept();
        CHECK(form.Values()[0] == 1 && form.Values()[11] == 9);

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(BoneScaleFixture(owner.Session(), root, "facial.pmd", {0, 0, 0, 1}, reason));
        CHECK(owner.SetLanguage(code));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_MULTIPLY_BONE_FRAMES)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog =
                    dynamic_cast<qt::BoneScaleDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->Values() == BoneScaleIdentity;
                dialog->Field(0)->setText("2.0");
                dialog->Field(1)->setText("0.5");
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty());
            if (!accept)
                CHECK(owner.Session().Revision() == revision);
            else {
                CHECK(owner.Session().Revision() != revision && owner.Session().CanUndo());
                const auto& keys = owner.Session().Document().models[0].boneKeys;
                CHECK(keys.sparse[0].value.position[0] == 6.5f &&
                      keys.sparse[2].value.position[0] == 3.f &&
                      keys.unused.at(99999).position[0] == 6.5f);
            }
        }
        CHECK(owner.Session().Undo(reason) &&
              owner.Session().Document().models[0].boneKeys.sparse[0].value.position[0] == 3);
        CHECK(owner.Session().Redo(reason) &&
              owner.Session().Document().models[0].boneKeys.sparse[0].value.position[0] == 6.5f);
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        owner.Session().Pause();
        CHECK(owner.Session().SelectModel(255, reason));
        owner.Refresh();
        CHECK(!action->isEnabled() && !owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::PhysicsKeysDialog form(language);
        const auto expected =
            reference[QString("physics_keys.") + (language.Code() == "en" ? "en" : "ja")]
                .toObject();
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.Template().id == expected["id"].toInt() &&
              form.size() == QSize(298, (70 * unitY + 4) / 8));
        CHECK(form.font().families().front() == QString::fromUtf8(form.Template().fontFamily));
        if (language.Code() != "zh_CN")
            CHECK(form.windowTitle() == expected["title"].toString());
        for (int i = 0; i < controls.size(); ++i) {
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget &&
                  widget->geometry() ==
                      QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                            (r[2].toInt() * 7 + 2) / 4,
                            control["kind"] == "Combobox" ? unitY + 8
                                                          : (r[3].toInt() * unitY + 4) / 8));
            if (language.Code() != "zh_CN") {
                if (auto* label = qobject_cast<QLabel*>(widget))
                    CHECK(label->text() == control["text"].toString());
                if (auto* button = qobject_cast<QPushButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        CHECK(form.Choice()->count() == 2 && form.Choice()->currentIndex() == 0 &&
              form.PhysicsEnabled());
        CHECK(form.Choice()->focusPolicy() == Qt::ClickFocus);
        CHECK(form.Choice()->itemText(0) == language.Text("dialog.numeric.physics_keys.on"));
        if (language.Code() != "zh_CN") {
            CHECK(form.Choice()->itemText(0) == (language.Code() == "en"
                                                     ? "ON (X mark)"
                                                     : QString::fromUtf8("ON (X\xe5\x8d\xb0)")));
            CHECK(form.Choice()->itemText(1) == "OFF");
        }
        form.Choice()->setCurrentIndex(1);
        CHECK(!form.PhysicsEnabled());
        QTest::keyClick(form.Choice(), Qt::Key_Up);
        CHECK(form.PhysicsEnabled());

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(PhysicsKeyFixture(owner.Session(), root, "articulated.pmx", false, 1, reason));
        CHECK(owner.SetLanguage(code) && owner.Save(root / "physics-dialog.pmm"));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_TOGGLE_FRAME_PHYSICS)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog =
                    dynamic_cast<qt::PhysicsKeysDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->PhysicsEnabled();
                dialog->Choice()->setCurrentIndex(1);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty() && !owner.Session().Dirty());
            CHECK((owner.Session().Revision() != revision) == accept);
        }
        CHECK(owner.Session().CanUndo() && owner.Session().Undo(reason) &&
              owner.Session().Redo(reason));
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        owner.Session().Pause();
        CHECK(owner.Session().SelectModel(255, reason));
        owner.Refresh();
        CHECK(!action->isEnabled() && !owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"})
        for (bool calculation : {false, true}) {
            CHECK(language.Set(code));
            qt::ModelOrderDialog form(calculation, language, {{7, "first"}, {254, "second"}});
            const QString kind = calculation ? "calculation_order" : "draw_order";
            const auto expected =
                reference[kind + "." + (language.Code() == "en" ? "en" : "ja")].toObject();
            const int unitY = language.Code() == "en" ? 15 : 12;
            CHECK(form.Template().id == expected["id"].toInt());
            const auto controls = expected["controls"].toArray();
            const auto button = [&](int id) {
                for (auto* b : form.findChildren<QPushButton*>())
                    if (b->property("mmdControlId").toInt() == id)
                        return b;
                return static_cast<QPushButton*>(nullptr);
            };
            for (int i = 0; i < controls.size(); ++i) {
                const auto control = controls[i].toObject();
                const auto r = control["rect"].toArray();
                auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
                CHECK(widget &&
                      widget->geometry() ==
                          QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                                (r[2].toInt() * 7 + 2) / 4,
                                control["kind"] == "Listbox"
                                    ? (((r[3].toInt() * unitY + 4) / 8 - 4) / unitY) * unitY + 4
                                    : (r[3].toInt() * unitY + 4) / 8));
                if (language.Code() != "zh_CN") {
                    if (auto* label = qobject_cast<QLabel*>(widget))
                        CHECK(label->text() == control["text"].toString());
                    if (auto* b = qobject_cast<QPushButton*>(widget))
                        CHECK(b->text() == control["text"].toString());
                }
            }
            CHECK(form.Rows()->currentRow() == -1 && form.Rows()->focusPolicy() == Qt::ClickFocus);
            button(630)->click();
            button(631)->click();
            button(633)->click();
            CHECK(form.Order() == std::vector<std::uint8_t>({7, 254}) && form.result() == 0);
            form.Rows()->setCurrentRow(1);
            button(631)->click();
            CHECK(form.Rows()->currentRow() == 1);
            button(630)->click();
            CHECK(form.Order() == std::vector<std::uint8_t>({254, 7}) &&
                  form.Rows()->currentRow() == 0);
            button(630)->click();
            CHECK(form.Order() == std::vector<std::uint8_t>({254, 7}));
            button(632)->click();
            CHECK(form.result() == QDialog::Accepted);

            qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
            for (int i = 0; i < 3; ++i)
                CHECK(owner.Session().ImportModel(root / "articulated.pmx", reason));
            CHECK(owner.Session().ImportAccessory(root / "bound.x", reason));
            CHECK(owner.SetLanguage(code) && owner.Save(root / "order-dialog.pmm"));
            QAction* action = nullptr;
            for (auto* item : owner.findChildren<QAction*>())
                if (item->property("mmdCommandId").toInt() ==
                    (calculation ? MDS_CMD_MODEL_CALCULATION_ORDER : MDS_CMD_MODEL_DRAW_ORDER))
                    action = item;
            CHECK(action && action->isEnabled());
            const auto revision = owner.Session().Revision();
            for (bool accept : {false, true}) {
                bool interacted = false;
                QTimer::singleShot(0, &owner, [&] {
                    auto* dialog =
                        dynamic_cast<qt::ModelOrderDialog*>(QApplication::activeModalWidget());
                    if (!dialog) {
                        QApplication::closeAllWindows();
                        return;
                    }
                    interacted = dialog->Order() == std::vector<std::uint8_t>({0, 1, 2});
                    dialog->Rows()->setCurrentRow(2);
                    for (auto* b : dialog->findChildren<QPushButton*>())
                        if (b->property("mmdControlId").toInt() == 630)
                            b->click();
                    if (accept)
                        dialog->accept();
                    else
                        dialog->reject();
                });
                action->trigger();
                CHECK(interacted && owner.LastError().isEmpty() && !owner.Session().Dirty());
                CHECK((owner.Session().Revision() != revision) == accept);
            }
            const auto orderKind =
                calculation ? format::ModelOrderKind::Calculation : format::ModelOrderKind::Draw;
            CHECK(format::PmmModelOrder(owner.Session().Document(), orderKind) ==
                  std::vector<std::uint8_t>({0, 2, 1}));
            CHECK(owner.Session().Selected().model == (calculation ? 2 : 255));
            for (int id :
                 {panel::kMainComboModel, panel::kMainComboNormal, panel::kMainComboGround}) {
                auto* combo = qobject_cast<QComboBox*>(owner.Control(id));
                CHECK(combo && combo->count() == 4);
                CHECK(combo->itemData(2).toInt() == (calculation ? 1 : 2));
                CHECK(combo->itemData(3).toInt() == (calculation ? 2 : 1));
            }
            CHECK(owner.Session().Play(reason));
            owner.Refresh();
            CHECK(!action->isEnabled() && !owner.isVisible());
        }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::AccessoryOrderDialog form(language, {{7, "first"}, {42, "second"}, {254, "third"}}, 0);
        const auto expected =
            reference[QString("accessory_order.") + (language.Code() == "en" ? "en" : "ja")]
                .toObject();
        CHECK(form.Template().id == expected["id"].toInt());
        const auto controls = expected["controls"].toArray();
        const int baseY = language.Code() == "en" ? 15 : 12;
        for (int i = 0; i < controls.size(); ++i) {
            const auto control = controls[i].toObject();
            const auto rect = control["rect"].toArray();
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            const int height = (rect[3].toInt() * baseY + 4) / 8;
            CHECK(widget &&
                  widget->geometry() ==
                      QRect((rect[0].toInt() * 7 + 2) / 4, (rect[1].toInt() * baseY + 4) / 8,
                            (rect[2].toInt() * 7 + 2) / 4,
                            control["kind"] == "Listbox" ? ((height - 4) / baseY) * baseY + 4
                                                         : height));
        }
        const auto button = [&](qt::AccessoryOrderDialog& dialog, int id) {
            for (auto* item : dialog.findChildren<QPushButton*>())
                if (item->property("mmdControlId").toInt() == id)
                    return item;
            return static_cast<QPushButton*>(nullptr);
        };
        CHECK(form.Rows()->currentRow() == -1 && form.Name()->text().isEmpty());
        for (const char* text : {"-1", "999", "1x", "  +1suffix", "abc", "0"}) {
            form.Split()->setText(text);
            const bool one = text[0] == '1' || text[0] == ' ';
            const int value = text[0] == '9' ? 3 : one ? 1 : 0;
            CHECK(form.SplitOrder() == value &&
                  form.Rows()->currentRow() == (value < 3 ? value : -1));
            CHECK(form.Name()->text().isEmpty());
            if (!one)
                CHECK(form.Split()->text() == QString::number(value));
        }
        form.Rows()->setCurrentRow(2);
        CHECK(form.Name()->text() == "third");
        form.Name()->setText("renamed");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(form.Name(), &enter);
        CHECK(form.Rows()->item(2)->text() == "renamed" && !form.result());
        form.Name()->setText("pending");
        button(form, 630)->click();
        CHECK(form.Order()[1].first == 254 && form.Order()[1].second == "renamed");
        CHECK(form.Name()->text() == "pending");
        button(form, 632)->click();
        CHECK(form.result() == QDialog::Accepted);

        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(owner.SetLanguage(code));
        if (std::string(code) == "ja")
            CHECK(owner.Session().ImportModel(root / "triangle.pmx", reason));
        for (const char* file : {"bound-varied.x", "sphere-mul.x", "sphere-add.x"})
            CHECK(owner.Session().ImportAccessory(root / file, reason));
        CHECK(owner.Save(root / "accessory-dialog.pmm"));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_ACCESSORY_SETTINGS)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        const auto frames = owner.Session().Runtime().Counters().frames;
        const auto* runtime = &owner.Session().Runtime();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog =
                    dynamic_cast<qt::AccessoryOrderDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->Order()[2].first == 2;
                dialog->Rows()->setCurrentRow(2);
                dialog->Name()->setText("renamed");
                QCoreApplication::sendEvent(dialog->Name(), &enter);
                button(*dialog, 630)->click();
                dialog->Split()->setText("1suffix");
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty() && !owner.Session().Dirty());
            CHECK((owner.Session().Revision() != revision) == accept);
        }
        const auto rows = format::PmmAccessoryOrder(owner.Session().Document());
        CHECK(rows[0].slot == 0 && rows[1].slot == 2 && rows[2].slot == 1 &&
              std::string(rows[1].name.data()) == "renamed");
        auto* combo = qobject_cast<QComboBox*>(owner.Control(panel::kAccessoryCombo));
        CHECK(combo->itemData(1).toInt() == 2 && combo->itemText(1) == "renamed");
        CHECK(owner.Session().Document().physics.accessoryRenderOrder == 1 &&
              owner.Session().Selected().accessory == 0);
        auto* scope = qobject_cast<QComboBox*>(owner.Control(panel::kRegisterScopeCombo));
        CHECK(scope && scope->count() == 7 && scope->currentIndex() == 0);
        CHECK(scope->itemText(5) == "renamed");
        CHECK(&owner.Session().Runtime() == runtime && runtime->Counters().frames == frames);
        CHECK(owner.Save(root / "accessory-dialog.pmm") &&
              owner.Open(root / "accessory-dialog.pmm"));
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled() && !owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::FrameShiftDialog form(language);
        const auto expected = reference[QString("frame_shift.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        CHECK(form.Template().id == expected["id"].toInt());
        const auto rect = expected["rect"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.size() == QSize((rect[2].toInt() * 7 + 2) / 4, (rect[3].toInt() * unitY + 4) / 8));
        if (language.Code() != "zh_CN")
            CHECK(form.windowTitle() == expected["title"].toString());
        const auto controls = expected["controls"].toArray();
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            CHECK(widget->geometry() == QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                                              (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
            CHECK(widget->property("mmdControlId").toInt() == control["id"].toInt());
            if (auto* button = qobject_cast<QPushButton*>(widget)) {
                CHECK(button->isDefault() == (control["id"].toInt() == 1));
                if (language.Code() != "zh_CN")
                    CHECK(button->text() == control["text"].toString());
            } else
                CHECK(widget == form.Field());
        }
        CHECK(form.Field()->text() == "0" && form.Field()->selectedText() == "0");
        for (const auto& [text, value] : std::vector<std::pair<QString, std::int32_t>>{
                 {" -15tail", -15}, {"+7.5", 7}, {"nonsense", 0}, {"--1", 0},
                 {"2147483648", INT_MAX}, {"-2147483649", INT_MIN},
                 {"00000000000000000009", 0}}) {
            form.Field()->setText(text);
            form.accept();
            CHECK(form.Value() == value && form.result() == QDialog::Accepted);
        }
        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(MorphPurgeFixture(owner.Session(), root, "morph-panels.pmx", true, 2, reason));
        CHECK(owner.SetLanguage(code));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_SHIFT_MOUTH_FRAMES)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog = dynamic_cast<qt::FrameShiftDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->Field()->text() == "0";
                dialog->Field()->setText("-15suffix");
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty() && (owner.Session().Revision() != revision) == accept);
        }
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        owner.Session().Pause();
        CHECK(owner.Session().SelectModel(255, reason));
        owner.Refresh();
        CHECK(!action->isEnabled() && !owner.isVisible());
    }
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(language.Set(code));
        qt::BlinkDialog form(language);
        const auto expected =
            reference[QString("blink.") + (language.Code() == "en" ? "en" : "ja")].toObject();
        const auto controls = expected["controls"].toArray();
        const int unitY = language.Code() == "en" ? 15 : 12;
        CHECK(form.Template().id == expected["id"].toInt() && controls.size() == 6);
        CHECK(form.size() == QSize(175, (50 * unitY + 4) / 8));
        CHECK(form.Field(0)->text().isEmpty() && form.Field(1)->text().isEmpty());
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = form.findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            CHECK(widget->geometry() ==
                  QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                        (r[2].toInt() * 7 + 2) / 4, (r[3].toInt() * unitY + 4) / 8));
            CHECK(widget->property("mmdControlId").toInt() == control["id"].toInt());
            if (auto* button = qobject_cast<QPushButton*>(widget))
                CHECK(button->isDefault() == (control["id"].toInt() == 1));
            if (language.Code() != "zh_CN") {
                if (auto* label = qobject_cast<QLabel*>(widget))
                    CHECK(label->text() == control["text"].toString());
                if (auto* button = qobject_cast<QPushButton*>(widget))
                    CHECK(button->text() == control["text"].toString());
            }
        }
        form.Field(0)->setText(" -15tail");
        form.Field(1)->setText("2147483648");
        form.accept();
        CHECK(form.Values()[0] == -15 && form.Values()[1] == INT_MAX);
        qt::EditorWindow owner(std::filesystem::absolute(argv[2]), true);
        CHECK(owner.Session().ImportModel(root / "blink.pmx", reason));
        CHECK(owner.SetLanguage(code));
        QAction* action = nullptr;
        for (auto* item : owner.findChildren<QAction*>())
            if (item->property("mmdCommandId").toInt() == MDS_CMD_REGISTER_BLINKING)
                action = item;
        CHECK(action && action->isEnabled());
        const auto revision = owner.Session().Revision();
        for (bool accept : {false, true}) {
            bool interacted = false;
            QTimer::singleShot(0, &owner, [&] {
                auto* dialog = dynamic_cast<qt::BlinkDialog*>(QApplication::activeModalWidget());
                if (!dialog) {
                    QApplication::closeAllWindows();
                    return;
                }
                interacted = dialog->Field(0)->text().isEmpty() &&
                             dialog->Field(1)->text().isEmpty() &&
                             dialog->focusWidget() == dialog->Field(0);
                QTest::keyClick(dialog->Field(0), Qt::Key_Tab);
                interacted &= dialog->focusWidget() == dialog->Field(1);
                dialog->Field(0)->setText("0tail");
                dialog->Field(1)->setText("1.5");
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action->trigger();
            CHECK(interacted && owner.LastError().isEmpty());
            CHECK((owner.Session().Revision() != revision) == accept);
        }
        CHECK(owner.Session().Document().timeline.lastRegisteredFrame == 13);
        CHECK(owner.Session().Play(reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        owner.Session().Pause();
        CHECK(owner.Session().SelectModel(255, reason));
        owner.Refresh();
        CHECK(!action->isEnabled());
        CHECK(owner.Session().ImportModel(root / "articulated.pmd", reason));
        owner.Refresh();
        CHECK(action->isEnabled() && !owner.Session().HasBlinkMorph());
        const auto missingRevision = owner.Session().Revision();
        action->trigger();
        CHECK(owner.Session().Revision() == missingRevision && owner.LastError().isEmpty());
        CHECK(!owner.isVisible());
    }
    std::cout
        << "Numeric dialogs: binary geometry/labels, tab order, formatting, cancel and editor publication passed\n";
}
