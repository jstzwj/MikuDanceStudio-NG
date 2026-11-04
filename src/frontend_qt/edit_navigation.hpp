#pragma once
#include <QString>

class QObject;
class QEvent;

namespace mikudancestudio::qt {
// Native EDIT's Ctrl+arrows follow legacy line-break opportunities, rather
// than Qt's word punctuation rules. Positions are UTF-16 offsets.
int EditWordPosition(const QString& text, int cursor, bool forward);
bool HandleEditNavigation(QObject* object, QEvent* event);
}
