#pragma once
#include <QColor>
#include <QPalette>
#include <QRect>
#include <QStyle>
class QPainter;
class QStyleOptionSlider;
class QStyleOptionButton;
class QFont;
class QString;
class QImage;
namespace mikudancestudio::ui {
struct TrackbarGeometry;
}
namespace mikudancestudio::qt {
bool DrawWindowsListRow(QPainter&, const QRect&, const QFont&, const QString&, bool selected,
                        bool enabled);
bool DrawWindowsListBorder(QPainter&, const QRect&, QStyle::State);
bool DrawWindowsComboText(QImage&, const QRect&, const QFont&, const QString&, bool enabled);
bool DrawWindowsButton(QPainter&, const QStyleOptionButton&, const QFont&,
                       const QRect* indicator = nullptr, bool radio = false,
                       bool showMnemonic = false);
bool DrawWindowsTrackbar(QPainter&, const QStyleOptionSlider&, const ui::TrackbarGeometry&);
void ConfigureWindowsListPalette(QPalette& palette);
bool DrawWindowsButtonIndicator(QPainter&, const QRect&, const QColor&, QStyle::State, bool radio);
void DrawWindowsWindowBorder(QPainter&, const QRect&);
void DrawWindowsSunkenBorder(QPainter&, const QRect&);
}
