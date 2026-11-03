#pragma once
#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QVariant>

namespace mikudancestudio::qt {
namespace detail {
// Some input contexts (including Qt 6.8's Windows IMM implementation) commit
// their cached preedit before cancelling the native composition in reset().
// Escape must discard that synchronous commit without rewriting the edit's
// text, selection or undo stack. Only the reset call owns this filter.
class DiscardPreeditCommit final : public QObject {
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::InputMethod)
            static_cast<QInputMethodEvent*>(event)->setCommitString({});
        return false;
    }
};
template <class Reset> void CancelEditPreedit(QObject* object, Reset&& reset) {
    const QPointer<QObject> target(object);
    DiscardPreeditCommit discard;
    object->installEventFilter(&discard);
    reset();
    if (target) {
        target->removeEventFilter(&discard);
        // Clear synthetic/replaced-context preedit as well. No text setter:
        // the edit retains the user's existing undo/redo history.
        QInputMethodEvent cancel;
        QCoreApplication::sendEvent(target, &cancel);
    }
}
}
// Call before application-specific Enter/Escape handling. Qt text edits own
// the preedit, while MMD commands must see only the eventual committed text.
inline bool HandleEditComposition(QObject* object, QEvent* event) {
    if (!qobject_cast<QLineEdit*>(object) && !qobject_cast<QPlainTextEdit*>(object))
        return false;
    constexpr const char* property = "_mmdImePreedit";
    if (event->type() == QEvent::InputMethod)
        object->setProperty(property,
                            !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty());
    else if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide)
        object->setProperty(property, false);
    else if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
             object->property(property).toBool()) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Escape)
            return false;
        if (event->type() == QEvent::KeyPress) {
            if (key == Qt::Key_Escape) {
                detail::CancelEditPreedit(object, [] { QGuiApplication::inputMethod()->reset(); });
            } else
                QGuiApplication::inputMethod()->commit();
        }
        event->accept();
        return true;
    }
    return false;
}
}
