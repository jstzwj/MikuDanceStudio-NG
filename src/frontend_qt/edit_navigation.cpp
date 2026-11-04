#include "edit_navigation.hpp"
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextBoundaryFinder>
#include <QVariant>
#include <algorithm>

namespace mikudancestudio::qt {
namespace {
bool BreakSpace(QChar c) {
    return c.isSpace() && c != QChar(0x00a0) && c != QChar(0x202f);
}

bool LegacyBoundary(QTextBoundaryFinder& finder, const QString& text, int position) {
    if (position == 0 || position == text.size())
        return true;
    const auto left = text[position - 1], right = text[position];
    if (BreakSpace(left) && !BreakSpace(right))
        return true;
    if ((left == '-' && right != '-') || (right == '-' && left.isDigit()))
        return true;
    // The original Windows EDIT uses the legacy Uniscribe line-breaking
    // convention: currency/sign prefixes and opening brackets start a run;
    // ASCII slash does not end an alphabetic run. Keep Unicode grapheme/CJK
    // boundaries in the shared engine, including surrogate/combining safety.
    if (QStringView(u"\\+$%([{").contains(right) && !BreakSpace(left)) {
        if (QStringView(u"\\+$%").contains(right) && left.isDigit())
            return false;
        return true;
    }
    if (left == '/' && right.isLetterOrNumber() && right.unicode() < 128)
        return false;
    finder.setPosition(position);
    return finder.isAtBoundary();
}

int MultilineWordPosition(const QTextCursor& cursor, bool forward) {
    const auto block = cursor.block();
    const auto offset = cursor.position() - block.position();
    const auto text = block.text();
    bool trailingSpace = true;
    for (int i = offset; i < text.size(); ++i)
        trailingSpace &= BreakSpace(text[i]);
    if (forward && trailingSpace && block.next().isValid()) {
        const auto next = block.next();
        int first = 0;
        while (first < next.text().size() && BreakSpace(next.text()[first]))
            ++first;
        return next.position() + first;
    }
    if (!forward && block.previous().isValid()) {
        int first = 0;
        while (first < block.text().size() && BreakSpace(block.text()[first]))
            ++first;
        if (offset <= first)
            return block.previous().position() + block.previous().text().size();
    }
    return block.position() + EditWordPosition(block.text(), offset, forward);
}
}

int EditWordPosition(const QString& text, int cursor, bool forward) {
    cursor = std::clamp(cursor, 0, int(text.size()));
    QTextBoundaryFinder finder(QTextBoundaryFinder::Line, text);
    if (forward) {
        while (cursor < text.size() && BreakSpace(text[cursor]))
            ++cursor;
        if (cursor < text.size())
            do {
                ++cursor;
            } while (!LegacyBoundary(finder, text, cursor));
        while (cursor < text.size() && BreakSpace(text[cursor]))
            ++cursor;
    } else if (cursor > 0) {
        --cursor;
        while (cursor > 0 && BreakSpace(text[cursor]))
            --cursor;
        while (!LegacyBoundary(finder, text, cursor))
            --cursor;
    }
    return cursor;
}

bool HandleEditNavigation(QObject* object, QEvent* event) {
    auto* edit = qobject_cast<QLineEdit*>(object);
    auto* multiline = qobject_cast<QPlainTextEdit*>(object);
    if ((!edit && !multiline) || object->property("_mmdImePreedit").toBool() ||
        (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride))
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    const auto modifiers = key->modifiers();
    if (!(modifiers & Qt::ControlModifier) ||
        (modifiers & ~(Qt::ControlModifier | Qt::ShiftModifier)))
        return false;
    const bool movement = key->key() == Qt::Key_Left || key->key() == Qt::Key_Right;
    if (!movement && (modifiers != Qt::ControlModifier ||
                      (key->key() != Qt::Key_A && key->key() != Qt::Key_Backspace &&
                       key->key() != Qt::Key_Delete)))
        return false;
    if (event->type() == QEvent::KeyPress) {
        if (multiline) {
            auto cursor = multiline->textCursor();
            if (movement) {
                cursor.setPosition(MultilineWordPosition(cursor, key->key() == Qt::Key_Right),
                                   modifiers & Qt::ShiftModifier ? QTextCursor::KeepAnchor
                                                                 : QTextCursor::MoveAnchor);
                multiline->setTextCursor(cursor);
                multiline->ensureCursorVisible();
            } else if (key->key() == Qt::Key_A) {
                if (cursor.selectionStart() != 0 ||
                    cursor.selectionEnd() != multiline->document()->characterCount() - 1)
                    multiline->selectAll();
            } else if (!multiline->isReadOnly()) {
                if (key->key() == Qt::Key_Backspace) {
                    // The English ANSI-font resource displays the DOS DEL
                    // glyph; the Japanese resource preserves the DEL code.
                    const auto character = multiline->property("mmdMultilineDelCharacter");
                    multiline->insertPlainText(
                        QString(QChar(character.isValid() ? character.toUInt() : 0x7fu)));
                } else {
                    if (!cursor.hasSelection())
                        cursor.setPosition(MultilineWordPosition(cursor, true),
                                           QTextCursor::KeepAnchor);
                    cursor.removeSelectedText();
                    multiline->setTextCursor(cursor);
                }
            }
            key->accept();
            return true;
        }
        if (movement) {
            const auto cursor = edit->cursorPosition();
            const int anchor = edit->hasSelectedText()
                                   ? edit->selectionStart() + edit->selectedText().size() -
                                         (cursor - edit->selectionStart())
                                   : cursor;
            const auto destination =
                EditWordPosition(edit->text(), cursor, key->key() == Qt::Key_Right);
            if (modifiers & Qt::ShiftModifier)
                edit->setSelection(anchor, destination - anchor);
            else
                edit->setCursorPosition(destination);
        } else if (key->key() == Qt::Key_A) {
            // Selecting an already fully selected EDIT preserves its active
            // end. A subsequent Shift+arrow must keep that selection anchor.
            if (edit->selectionStart() != 0 || edit->selectedText().size() != edit->text().size())
                edit->selectAll();
        } else if (!edit->isReadOnly()) {
            if (key->key() == Qt::Key_Backspace) {
                // Native single-line EDIT receives WM_CHAR(DEL) for this
                // chord and inserts it, including ordinary selection/undo.
                edit->insert(QString(QChar(0x7f)));
            } else {
                if (!edit->hasSelectedText())
                    edit->setSelection(
                        edit->cursorPosition(),
                        EditWordPosition(edit->text(), edit->cursorPosition(), true) -
                            edit->cursorPosition());
                edit->del();
            }
        }
    }
    key->accept();
    return true;
}
}
