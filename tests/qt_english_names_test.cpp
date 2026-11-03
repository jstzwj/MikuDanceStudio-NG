#include "english_name_dialog.hpp"
#include "editor_window.hpp"
#include "model_labels.hpp"
#include "edit_composition.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/english_name_edit.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTest>
#include <QInputMethodEvent>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 4 && QGuiApplication::platformName() == "offscreen");
    // Independently reproduce input contexts which synchronously commit their
    // cached preedit during reset (Qt's Windows IMM context does this). Escape
    // must not insert it, consume a selection, or discard existing edit history.
    for (const bool selected : {false, true}) {
        QLineEdit line;
        line.setText("prefix suffix");
        line.setCursorPosition(6);
        line.insert("!");
        if (selected)
            line.setSelection(0, 3);
        const auto text = line.text(), selection = line.selectedText();
        const auto cursor = line.cursorPosition(), start = line.selectionStart();
        const auto undo = line.isUndoAvailable(), redo = line.isRedoAvailable();
        qt::detail::CancelEditPreedit(&line, [&] {
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("\u65e5\u672c"), -2, 4);
            QCoreApplication::sendEvent(&line, &commit);
        });
        CHECK(line.text() == text && line.selectedText() == selection &&
              line.cursorPosition() == cursor && line.selectionStart() == start &&
              line.isUndoAvailable() == undo && line.isRedoAvailable() == redo);
        line.undo();
        CHECK(line.text() == "prefix suffix");
        line.redo();
        CHECK(line.text() == text);

        QPlainTextEdit multiline;
        multiline.setPlainText("prefix\nsuffix");
        auto textCursor = multiline.textCursor();
        textCursor.setPosition(6);
        textCursor.insertText("!");
        if (selected) {
            textCursor.setPosition(0);
            textCursor.setPosition(3, QTextCursor::KeepAnchor);
        }
        multiline.setTextCursor(textCursor);
        const auto multilineText = multiline.toPlainText();
        const auto multilineUndo = multiline.document()->isUndoAvailable();
        const auto multilineRedo = multiline.document()->isRedoAvailable();
        qt::detail::CancelEditPreedit(&multiline, [&] {
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("\u65e5\u672c"), -2, 4);
            QCoreApplication::sendEvent(&multiline, &commit);
        });
        CHECK(multiline.toPlainText() == multilineText &&
              multiline.textCursor().position() == textCursor.position() &&
              multiline.textCursor().anchor() == textCursor.anchor() &&
              multiline.document()->isUndoAvailable() == multilineUndo &&
              multiline.document()->isRedoAvailable() == multilineRedo);
        multiline.undo();
        CHECK(multiline.toPlainText() == "prefix\nsuffix");
        multiline.redo();
        CHECK(multiline.toPlainText() == multilineText);
    }
    auto* removedEdit = new QLineEdit;
    qt::detail::CancelEditPreedit(removedEdit, [&] { delete removedEdit; });
    const auto root = std::filesystem::absolute(argv[1]);
    QFile file(QString::fromLocal8Bit(argv[3]));
    CHECK(file.open(QIODevice::ReadOnly));
    const auto references = QJsonDocument::fromJson(file.readAll()).object();
    for (const char* language : {"en", "ja", "zh_CN"})
        for (const char* model : {"english.pmd", "lips.pmd", "english.pmx"}) {
            qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
            CHECK(window.SetLanguage(language));
            auto& session = window.Session();
            const auto command = [&]() -> QAction* {
                for (auto* action : window.findChildren<QAction*>())
                    if (action->property("mmdCommandId").toInt() == MDS_CMD_EDIT_ENGLISH_NAMES)
                        return action;
                return nullptr;
            };
            CHECK(command() && !command()->isEnabled());
            CHECK(session.ImportModel(root / model, reason));
            window.Refresh();
            if (session.Runtime().Resource(0)->SourceKind() == format::ModelKind::Pmx) {
                CHECK(!command()->isEnabled()); // Native enhancement submenu is PMD-only.
                command()->trigger();
                CHECK(!window.findChild<QDialog*>("mmd.englishNames"));
                continue;
            }
            CHECK(command()->isEnabled());
            const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
            const auto resource = session.Runtime().Resource(0);
            const auto undo = session.CanUndo(), redo = session.CanRedo();
            command()->trigger();
            auto* dialog = dynamic_cast<qt::EnglishNameDialog*>(
                window.findChild<QDialog*>("mmd.englishNames"));
            CHECK(dialog && dialog->Active() && !dialog->isVisible() && !window.isVisible());
            CHECK(!window.Control(panel::kMainComboModel)->isEnabled());
            const auto expected =
                references[QString("english_name.") + (std::string(language) == "en" ? "en" : "ja")]
                    .toObject();
            CHECK(dialog->Template().id == expected["id"].toInt());
            const int unitY = std::string(language) == "en" ? 14 : 12;
            const auto rect = expected["rect"].toArray();
            CHECK(dialog->size() ==
                  QSize((rect[2].toInt() * 7 + 2) / 4, (rect[3].toInt() * unitY + 4) / 8));
            const auto controls = expected["controls"].toArray();
            for (int i = 0; i < controls.size(); ++i) {
                auto* widget = dialog->findChild<QWidget*>(QString("numeric.control.%1").arg(i));
                CHECK(widget);
                const auto control = controls[i].toObject();
                const auto r = control["rect"].toArray();
                CHECK(widget->geometry() ==
                      QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * unitY + 4) / 8,
                            (r[2].toInt() * 7 + 2) / 4,
                            control["kind"] == "Combobox" ? unitY + 8
                                                          : (r[3].toInt() * unitY + 4) / 8));
            }
            const auto control = [&](int id) -> QWidget* {
                for (auto* widget : dialog->findChildren<QWidget*>())
                    if (widget->property("mmdControlId").toInt() == id)
                        return widget;
                return nullptr;
            };
            auto* name = qobject_cast<QLineEdit*>(control(667));
            auto* bone = qobject_cast<QLineEdit*>(control(672));
            auto* boneCombo = qobject_cast<QComboBox*>(control(669));
            auto* morph = qobject_cast<QLineEdit*>(control(676));
            auto* morphCombo = qobject_cast<QComboBox*>(control(673));
            auto* group = qobject_cast<QLineEdit*>(control(680));
            auto* comment = qobject_cast<QPlainTextEdit*>(control(668));
            CHECK(name && bone && boneCombo && morph && morphCombo && group && comment);
            CHECK(!comment->isReadOnly() && comment->lineWrapMode() == QPlainTextEdit::NoWrap);
            format::PmmModelEnglishNames composingNames;
            CHECK(session.ModelEnglishNames(0, composingNames, reason));
            name->setText("uncommitted raw text");
            name->selectAll();
            QInputMethodEvent preedit(QStringLiteral("\u306b\u307b\u3093"), {});
            QCoreApplication::sendEvent(name, &preedit);
            QTest::keyClick(name, Qt::Key_Return);
            format::PmmModelEnglishNames duringComposition;
            CHECK(session.ModelEnglishNames(0, duringComposition, reason) &&
                  duringComposition.name == composingNames.name && dialog->Active());
            QTest::keyClick(name, Qt::Key_Escape);
            CHECK(dialog->Active());
            QCoreApplication::sendEvent(name, &preedit);
            QInputMethodEvent committed;
            committed.setCommitString(QStringLiteral("\u65e5\u672c"));
            QCoreApplication::sendEvent(name, &committed);
            CHECK(name->text() == QStringLiteral("\u65e5\u672c"));
            QTest::keyClick(name, Qt::Key_Return);
            CHECK(session.ModelEnglishNames(0, duringComposition, reason) &&
                  QString::fromUtf8(editor::EnglishNameDisplay(duringComposition.name).c_str()) ==
                      QStringLiteral("\u65e5\u672c"));
            name->setText("new English model");
            QTest::keyClick(name, Qt::Key_Return);
            CHECK(dialog->Active());
            format::PmmModelEnglishNames names;
            CHECK(session.ModelEnglishNames(0, names, reason) && names.name == "new English model");
            bone->setText("first bone");
            if (boneCombo->count() > 1) {
                boneCombo->setCurrentIndex(1);
                CHECK(session.ModelEnglishNames(0, names, reason) &&
                      names.bones[0] == "first bone");
                CHECK(session.ActiveBone() == 1);
                boneCombo->setCurrentIndex(6);
                CHECK(session.ActiveBone() == -1 && session.Selected().bones.empty());
                boneCombo->setCurrentIndex(9);
                CHECK(session.ActiveBone() == -1);
                boneCombo->setCurrentIndex(0);
                CHECK(bone->text() == "first bone");
            } else {
                qobject_cast<QPushButton*>(control(670))->click();
                CHECK(session.ModelEnglishNames(0, names, reason) &&
                      names.bones[0] == "first bone");
            }
            bone->setText("cancel still applies");
            morph->setText("first morph edit");
            morphCombo->setCurrentIndex(1);
            CHECK(session.ModelEnglishNames(0, names, reason) &&
                  names.morphs[1] == "first morph edit");
            morph->setText("second morph edit");
            qobject_cast<QPushButton*>(control(674))->click();
            CHECK(session.ModelEnglishNames(0, names, reason) &&
                  names.morphs[2] == "second morph edit");
            group->setText(QString(60, 'g'));
            comment->setPlainText(QString(250, 'c') + "\nA\nB");
            dialog->reject(); // Native cancel/window close commits pending fields.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            CHECK(!window.findChild<QDialog*>("mmd.englishNames"));
            CHECK(window.Control(panel::kMainComboModel)->isEnabled());
            CHECK(session.ModelEnglishNames(0, names, reason));
            CHECK(names.bones[0] == "cancel still applie" && !names.enhanced);
            CHECK(names.comment == std::string(250, 'c') + "\nA\n");
            CHECK(names.groups[3] == std::string(49, 'g'));
            CHECK(qt::ModelTrackLabel(session, 0, 0, false, true) == "cancel still applie");
            CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames &&
                  session.Runtime().Resource(0) == resource && session.CanUndo() == undo &&
                  session.CanRedo() == redo);
            command()->trigger();
            dialog = dynamic_cast<qt::EnglishNameDialog*>(
                window.findChild<QDialog*>("mmd.englishNames"));
            CHECK(dialog && dialog->Active());
            auto* multiline = dialog->findChild<QPlainTextEdit*>();
            CHECK(multiline);
            multiline->setPlainText("first");
            auto cursor = multiline->textCursor();
            cursor.movePosition(QTextCursor::End);
            multiline->setTextCursor(cursor);
            QTest::keyClick(multiline, Qt::Key_Return, Qt::ControlModifier);
            CHECK(dialog->Active() && multiline->toPlainText() == "first\n");
            multiline->undo();
            CHECK(multiline->toPlainText() == "first");
            QTest::keyClick(multiline, Qt::Key_Return);
            CHECK(!dialog->Active()); // Both original templates omit ES_WANTRETURN.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            CHECK(session.ModelEnglishNames(0, names, reason) && names.enhanced &&
                  session.EnhancedModelDirty());
            CHECK(session.Play(reason));
            window.Refresh();
            CHECK(!command()->isEnabled());
            session.Pause();
            window.Refresh();
            command()->trigger();
            CHECK(session.UnloadModel(0, reason));
            window.Refresh();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            CHECK(!window.findChild<QDialog*>("mmd.englishNames") && !window.isVisible());
        }
    std::cout
        << "English-name dialog: original geometry, three languages, native commit/cancel and lifecycle passed\n";
    return 0;
}
