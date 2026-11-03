#pragma once
#include <QColor>
#include <QRect>
#include <QStyle>
class QPainter;
namespace mikudancestudio::qt {
bool DrawWindowsButtonIndicator(QPainter&, const QRect&, const QColor&, QStyle::State, bool radio);
}
