#include "numeric_dialog.hpp"
#include "edit_navigation.hpp"
#include "multiline_text_cases.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextCursor>
#include <QTextBoundaryFinder>
#include <iostream>

using namespace mikudancestudio;
namespace {
class Dialog final : public qt::TemplateDialog {
public:
    explicit Dialog(const qt::Language& language)
        : TemplateDialog(language.Code() == "en" ? ui::kNumericEnglishNameEn
                                                 : ui::kNumericEnglishNameJa,
                         language, nullptr) {}
    QWidget* Field(int control) {
        return control == 668 ? static_cast<QWidget*>(Multiline(668)) : Edit(667);
    }
};
class Field {
public:
    explicit Field(QWidget* widget)
        : widget_(widget), line_(qobject_cast<QLineEdit*>(widget)),
          multiline_(qobject_cast<QPlainTextEdit*>(widget)) {}
    QWidget* Widget() const { return widget_; }
    bool Multiline() const { return multiline_ != nullptr; }
    QString text() const { return line_ ? line_->text() : multiline_->toPlainText(); }
    void setText(const QString& value) {
        if (line_)
            line_->setText(value);
        else
            multiline_->setPlainText(value);
    }
    void setReadOnly(bool value) {
        if (line_)
            line_->setReadOnly(value);
        else
            multiline_->setReadOnly(value);
    }
    void setSelection(int first, int count) {
        if (line_)
            line_->setSelection(first, count);
        else {
            auto cursor = multiline_->textCursor();
            cursor.setPosition(first);
            cursor.setPosition(first + count, QTextCursor::KeepAnchor);
            multiline_->setTextCursor(cursor);
        }
    }
    int cursorPosition() const {
        return line_ ? line_->cursorPosition() : multiline_->textCursor().position();
    }
    void setCursorPosition(int position) { setSelection(position, 0); }
    QJsonArray Selection() const {
        if (line_) {
            const auto first =
                line_->hasSelectedText() ? line_->selectionStart() : line_->cursorPosition();
            return {first, first + line_->selectedText().size()};
        }
        const auto cursor = multiline_->textCursor();
        return {cursor.selectionStart(), cursor.selectionEnd()};
    }
    void undo() {
        if (line_)
            line_->undo();
        else
            multiline_->undo();
    }
    void redo() {
        if (line_)
            line_->redo();
        else
            multiline_->redo();
    }

private:
    QWidget* widget_;
    QLineEdit* line_;
    QPlainTextEdit* multiline_;
};
QJsonArray Selection(const Field& field) {
    return field.Selection();
}
bool CheckCase(Field& field, const QJsonObject& c, const QString& text, bool readOnly) {
    field.setReadOnly(readOnly);
    field.setText(text);
    // Single-line EM_SETSEL normalizes its arguments; multiline EDIT retains
    // its active end. Keyboard-created reverse selections have their own corpus.
    const auto anchor = c["anchor"].toInt(), cursor = c["cursor"].toInt();
    field.setSelection(field.Multiline() ? anchor : std::min(anchor, cursor),
                       field.Multiline() ? cursor - anchor : std::abs(cursor - anchor));
    const auto selection = Selection(field);
    const auto key = c["key"].toString();
    const auto code = key == "left"     ? Qt::Key_Left
                      : key == "right"  ? Qt::Key_Right
                      : key == "all"    ? Qt::Key_A
                      : key == "delete" ? Qt::Key_Delete
                                        : Qt::Key_Backspace;
    const auto modifiers =
        Qt::ControlModifier | (c["shift"].toBool() ? Qt::ShiftModifier : Qt::NoModifier);
    QTest::keyClick(field.Widget(), code, modifiers);
    const bool rejected = readOnly && (code == Qt::Key_Backspace || code == Qt::Key_Delete);
    const auto expected = rejected || !c.contains("result") ? text : c["result"].toString();
    const auto expectedSelection = rejected ? selection : c["selection"].toArray();
    if (field.text() != expected || Selection(field) != expectedSelection) {
        std::cerr << "key=" << key.toStdString() << " text=" << text.toUtf8().constData()
                  << " anchor=" << anchor << " cursor=" << cursor
                  << " shift=" << c["shift"].toBool() << " readonly=" << readOnly << " selection="
                  << QJsonDocument(Selection(field)).toJson(QJsonDocument::Compact).constData()
                  << " expected="
                  << QJsonDocument(expectedSelection).toJson(QJsonDocument::Compact).constData()
                  << '\n';
        return false;
    }
    if (expected != text) {
        field.undo();
        if (field.text() != text)
            return false;
        field.redo();
        if (field.text() != expected)
            return false;
    }
    return true;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 3 || QGuiApplication::platformName() != "offscreen")
        return 2;
    QJsonArray records;
    for (int i = 1; i < argc; ++i) {
        QFile fixture(QString::fromLocal8Bit(argv[i]));
        if (!fixture.open(QIODevice::ReadOnly))
            return 2;
        const auto reference = QJsonDocument::fromJson(fixture.readAll()).object();
        if (reference["originalSha256"] !=
            "07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1")
            return 2;
        for (const auto record : reference["records"].toArray())
            records.append(record);
    }
    unsigned comparisons = 0;
    unsigned failures = 0;
    for (const char* code : {"en", "ja", "zh_CN"}) {
        qt::Language language;
        if (!language.Set(code))
            return 1;
        Dialog dialog(language);
        for (const auto entry : records) {
            const auto record = entry.toObject();
            if (record["template"].toInt() != (language.Code() == "en" ? 666 : 665))
                continue;
            Field field(dialog.Field(record["control"].toInt()));
            const auto text = record["text"].toString();
            bool positionsMatch = true;
            for (const bool forward : {false, true}) {
                const auto positions = record[forward ? "right" : "left"].toArray();
                if (positions.size() != text.size() + 1)
                    return 1;
                for (int cursor = 0; cursor < positions.size(); ++cursor) {
                    int actual;
                    if (field.Multiline()) {
                        field.setReadOnly(false);
                        field.setText(text);
                        field.setCursorPosition(cursor);
                        QTest::keyClick(field.Widget(), forward ? Qt::Key_Right : Qt::Key_Left,
                                        Qt::ControlModifier);
                        actual = field.cursorPosition();
                    } else
                        actual = qt::EditWordPosition(text, cursor, forward);
                    if (actual != positions[cursor].toInt()) {
                        std::cerr << "word text=" << text.toUtf8().constData()
                                  << " cursor=" << cursor << " forward=" << forward
                                  << " actual=" << actual
                                  << " expected=" << positions[cursor].toInt() << '\n';
                        ++failures;
                        positionsMatch = false;
                    }
                    ++comparisons;
                }
            }
            if (!positionsMatch)
                continue;
            for (const auto c : record["selections"].toArray())
                for (const bool readOnly : {false, true}) {
                    if (!CheckCase(field, c.toObject(), text, readOnly))
                        return 1;
                    ++comparisons;
                }
            for (const auto entry : record["sequences"].toArray()) {
                const auto sequence = entry.toObject();
                field.setReadOnly(false);
                field.setText(text);
                field.setCursorPosition(sequence["start"].toInt());
                const auto keys = sequence["keys"].toArray();
                const auto expected = sequence["selections"].toArray();
                for (int step = 0; step < keys.size(); ++step) {
                    const auto key = keys[step].toString();
                    QTest::keyClick(
                        field.Widget(),
                        key.endsWith("left")    ? Qt::Key_Left
                        : key.endsWith("right") ? Qt::Key_Right
                                                : Qt::Key_A,
                        Qt::ControlModifier |
                            (key.startsWith("shift-") ? Qt::ShiftModifier : Qt::NoModifier));
                    if (Selection(field) != expected[step].toArray() || field.text() != text) {
                        std::cerr << "sequence text=" << text.toUtf8().constData()
                                  << " start=" << sequence["start"].toInt() << " step=" << step
                                  << " key=" << key.toStdString() << " actual="
                                  << QJsonDocument(Selection(field))
                                         .toJson(QJsonDocument::Compact)
                                         .constData()
                                  << " expected="
                                  << QJsonDocument(expected[step].toArray())
                                         .toJson(QJsonDocument::Compact)
                                         .constData()
                                  << '\n';
                        return 1;
                    }
                    ++comparisons;
                }
            }
        }
        if (dialog.isVisible())
            return 1;
        // Complete Unicode is an intentional extension over ES_OEMCONVERT.
        // Keep the shared word policy safe for UTF-16/grapheme boundaries and
        // verify actual document undo, without inheriting native data loss.
        auto* multiline = qobject_cast<QPlainTextEdit*>(dialog.Field(668));
        for (const auto& text : MultilineUnicodeCases()) {
            multiline->setPlainText(text);
            QTest::keyClick(multiline, Qt::Key_A, Qt::ControlModifier);
            if (multiline->textCursor().selectionStart() != 0 ||
                multiline->textCursor().selectionEnd() != text.size())
                return 1;
            QTest::keyClick(multiline, Qt::Key_Backspace);
            if (!multiline->toPlainText().isEmpty())
                return 1;
            multiline->undo();
            if (multiline->toPlainText() != text)
                return 1;
            multiline->redo();
            if (!multiline->toPlainText().isEmpty())
                return 1;
            multiline->undo();
            multiline->moveCursor(QTextCursor::Start);
            QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
            for (unsigned step = 0; step < 32 && multiline->textCursor().position() < text.size();
                 ++step) {
                const auto before = multiline->textCursor().position();
                QTest::keyClick(multiline, Qt::Key_Right, Qt::ControlModifier);
                const auto after = multiline->textCursor().position();
                graphemes.setPosition(after);
                if (after <= before || !graphemes.isAtBoundary())
                    return 1;
            }
            if (multiline->textCursor().position() != text.size() ||
                multiline->toPlainText() != text)
                return 1;
        }
        // IME owns every editing chord during preedit. The portable MMD key
        // filter must not turn composition navigation/deletion into an edit.
        for (const int control : {667, 668}) {
            auto* widget = dialog.Field(control);
            widget->setProperty("_mmdImePreedit", true);
            for (const auto key :
                 {Qt::Key_Left, Qt::Key_Right, Qt::Key_A, Qt::Key_Backspace, Qt::Key_Delete}) {
                QKeyEvent event(QEvent::KeyPress, key, Qt::ControlModifier);
                if (qt::HandleEditNavigation(widget, &event))
                    return 1;
            }
            widget->setProperty("_mmdImePreedit", false);
        }
    }
    std::cout << comparisons << " original EDIT navigation/selection/read-only/undo comparisons\n";
    return failures ? 1 : 0;
}
