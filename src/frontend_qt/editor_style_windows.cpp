#include "editor_style_windows.hpp"
#include <QImage>
#include <QPainter>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <uxtheme.h>
#include <vssym32.h>

namespace mikudancestudio::qt {
void DrawWindowsWindowBorder(QPainter& painter, const QRect& rectangle) {
    const auto color = GetSysColor(COLOR_WINDOWFRAME);
    const QColor border(GetRValue(color), GetGValue(color), GetBValue(color));
    painter.fillRect(QRect(rectangle.left(), rectangle.top(), rectangle.width(), 1), border);
    painter.fillRect(QRect(rectangle.left(), rectangle.bottom(), rectangle.width(), 1), border);
    painter.fillRect(QRect(rectangle.left(), rectangle.top(), 1, rectangle.height()), border);
    painter.fillRect(QRect(rectangle.right(), rectangle.top(), 1, rectangle.height()), border);
}
bool DrawWindowsButtonIndicator(QPainter& painter, const QRect& logical, const QColor& background,
                                QStyle::State flags, bool radio) {
    const qreal ratio = painter.device()->devicePixelRatioF();
    const int width = qRound(logical.width() * ratio), height = qRound(logical.height() * ratio);
    if (width <= 0 || height <= 0)
        return false;
    const auto theme = OpenThemeDataForDpi(nullptr, L"Button", UINT(qRound(96 * ratio)));
    if (!theme)
        return false;
    const auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const auto bitmap =
        dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool painted = false;
    if (bitmap && bits) {
        const auto previous = SelectObject(dc, bitmap);
        QImage image(static_cast<unsigned char*>(bits), width, height, width * 4,
                     QImage::Format_RGB32);
        image.fill(background);
        const int state = !(flags & QStyle::State_Enabled)  ? 4
                          : flags & QStyle::State_Sunken    ? 3
                          : flags & QStyle::State_MouseOver ? 2
                                                            : 1;
        const int checked = flags & QStyle::State_NoChange ? 8 : flags & QStyle::State_On ? 4 : 0;
        const RECT rectangle{0, 0, width, height};
        painted = SUCCEEDED(DrawThemeBackground(theme, dc, radio ? BP_RADIOBUTTON : BP_CHECKBOX,
                                                state + checked, &rectangle, nullptr));
        GdiFlush();
        if (painted) {
            image.setDevicePixelRatio(ratio);
            painter.drawImage(logical.topLeft(), image);
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
    }
    if (dc)
        DeleteDC(dc);
    CloseThemeData(theme);
    return painted;
}
}
