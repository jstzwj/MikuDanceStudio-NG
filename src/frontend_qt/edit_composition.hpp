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
                const QPointer<QObject> target(object);
                QGuiApplication::inputMethod()->reset();
                // Clear the widget's preedit even for a context which did not
                // originate it (e.g. offscreen input or a replaced context).
                QInputMethodEvent cancel;
                if (target)
                    QCoreApplication::sendEvent(target, &cancel);
            } else
                QGuiApplication::inputMethod()->commit();
        }
        event->accept();
        return true;
    }
    return false;
}
}
