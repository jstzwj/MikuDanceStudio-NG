#include "multiline_hit.hpp"
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextLayout>
#include <algorithm>

namespace mikudancestudio::qt {
QTextCursor MultilineCursorForPosition(const QPlainTextEdit& field, const QPointF& point) {
    auto cursor = field.cursorForPosition(point.toPoint());
    const auto block = cursor.block();
    const auto* layout = block.layout();
    if (!layout || !layout->lineCount() || field.property("_mmdImePreedit").toBool())
        return cursor;
    // Preserve Qt's Unicode shaping/bidi policy instead of emulating the
    // original ANSI control's lossy character conversions.
    for (const auto character : block.text())
        if (character.unicode() > 0x7f)
            return cursor;
    QTextCursor start(block);
    const auto origin = field.cursorRect(start).topLeft();
    const auto firstLine = layout->lineAt(0);
    const auto y = point.y() - origin.y() + firstLine.y();
    auto line = firstLine;
    for (int index = 1; index < layout->lineCount(); ++index) {
        const auto next = layout->lineAt(index);
        if (next.y() > y)
            break;
        line = next;
    }
    // QTextLine resolves an exact half advance to its preceding cursor. EDIT
    // resolves it to the following cursor; 1/64 is QTextEngine's subpixel unit.
    const auto x = point.x() - origin.x() + firstLine.cursorToX(0) + qreal(1.0 / 64);
    cursor.setPosition(block.position() + line.xToCursor(x));
    return cursor;
}
}
