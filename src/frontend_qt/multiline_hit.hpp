#pragma once
#include <QPointF>
#include <QTextCursor>
class QPlainTextEdit;

namespace mikudancestudio::qt {
// Original EDIT retains the horizontal character column above/below the text
// and chooses the following character at an exact advance midpoint.
QTextCursor MultilineCursorForPosition(const QPlainTextEdit&, const QPointF&);
}
