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
class QWidget;
namespace mikudancestudio::ui {
struct TrackbarGeometry;
}
namespace mikudancestudio::qt {
bool DrawWindowsEditText(QPainter&, const QRect&, const QFont&, const QString&, bool english,
                         bool border, bool enabled, bool readOnly, bool autoScroll);
bool DrawWindowsListRow(QPainter&, const QRect&, const QFont&, const QString&, bool selected,
                        bool enabled, bool keyboardFocus);
bool DrawWindowsListBorder(QPainter&, const QRect&, QStyle::State);
bool DrawWindowsEditBorder(QPainter&, const QRect&, QStyle::State);
bool DrawWindowsComboText(QImage&, const QRect&, const QFont&, const QString&, bool enabled,
                          bool keyboardFocus);
bool DrawWindowsButton(QPainter&, const QStyleOptionButton&, const QFont&,
                       const QRect* indicator = nullptr, bool radio = false,
                       bool showMnemonic = false, const QWidget* widget = nullptr);
bool DrawWindowsTrackbar(QPainter&, const QStyleOptionSlider&, const ui::TrackbarGeometry&,
                         const QWidget* widget = nullptr);
void ConfigureWindowsListPalette(QPalette& palette);
bool DrawWindowsButtonIndicator(QPainter&, const QRect&, const QColor&, QStyle::State, bool radio);
void DrawWindowsWindowBorder(QPainter&, const QRect&);
void DrawWindowsSunkenBorder(QPainter&, const QRect&);
}
