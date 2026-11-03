#pragma once
#include <QColor>
#include <QPalette>
#include <QRect>
#include <QStyle>
class QPainter;
class QStyleOptionSlider;
namespace mikudancestudio::ui {
struct TrackbarGeometry;
}
namespace mikudancestudio::qt {
bool DrawWindowsTrackbar(QPainter&, const QStyleOptionSlider&, const ui::TrackbarGeometry&);
void ConfigureWindowsListPalette(QPalette& palette);
bool DrawWindowsButtonIndicator(QPainter&, const QRect&, const QColor&, QStyle::State, bool radio);
void DrawWindowsWindowBorder(QPainter&, const QRect&);
void DrawWindowsSunkenBorder(QPainter&, const QRect&);
}
