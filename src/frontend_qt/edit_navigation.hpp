#pragma once
#include <QKeyEvent>
#include <QLineEdit>
#include <QVariant>

namespace mikudancestudio::qt {
// Native single-line EDIT uses Ctrl+Delete for the remainder of the line,
// retaining an existing selection's usual deletion behavior. Qt normally
// deletes one word, and its platform bindings differ on macOS.
inline bool HandleEditDeleteTail(QObject* object, QEvent* event) {
    auto* edit = qobject_cast<QLineEdit*>(object);
    if (!edit || edit->property("_mmdImePreedit").toBool() ||
        (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride))
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Delete || key->modifiers() != Qt::ControlModifier)
        return false;
    if (event->type() == QEvent::KeyPress && !edit->isReadOnly()) {
        if (!edit->hasSelectedText())
            edit->setSelection(edit->cursorPosition(),
                               edit->text().size() - edit->cursorPosition());
        // Use the line control's edit operation, preserving undo/redo, change
        // notifications and the ordinary selection replacement contract.
        edit->del();
    }
    key->accept();
    return true;
}
}
