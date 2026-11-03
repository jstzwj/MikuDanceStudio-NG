#include "editor_style_windows.hpp"
#include "mikudancestudio/mmd_trackbar_geometry.hpp"
#include <QImage>
#include <QPainter>
#include <QStyleOptionSlider>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <uxtheme.h>
#include <vssym32.h>

namespace mikudancestudio::qt {
bool DrawWindowsTrackbar(QPainter& painter, const QStyleOptionSlider& option,
                         const ui::TrackbarGeometry& geometry) {
    const auto ratio = painter.device()->devicePixelRatioF();
    const int width = qRound(option.rect.width() * ratio);
    const int height = qRound(option.rect.height() * ratio);
    if (width <= 0 || height <= 0)
        return false;
    const auto theme = OpenThemeDataForDpi(nullptr, L"Trackbar", UINT(qRound(96 * ratio)));
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
        image.fill(option.palette.window().color());
        const auto rectangle = [&](const ui::TrackbarRectangle& r) {
            return RECT{qRound(r.x * ratio), qRound(r.y * ratio), qRound((r.x + r.width) * ratio),
                        qRound((r.y + r.height) * ratio)};
        };
        const auto channel = rectangle(geometry.channel), thumb = rectangle(geometry.thumb);
        const bool enabled = option.state & QStyle::State_Enabled;
        const int state = !enabled                              ? TUS_DISABLED
                          : option.state & QStyle::State_Sunken ? TUS_PRESSED
                          : (option.activeSubControls & QStyle::SC_SliderHandle) &&
                                  (option.state & QStyle::State_MouseOver)
                              ? TUS_HOT
                              : TUS_NORMAL;
        painted =
            SUCCEEDED(DrawThemeBackground(theme, dc, TKP_TRACK, TRS_NORMAL, &channel, nullptr));
        if (option.tickPosition != QSlider::NoTicks) {
            COLORREF color = GetSysColor(COLOR_3DSHADOW);
            GetThemeColor(theme, TKP_TICS, TSS_NORMAL, TMT_COLOR, &color);
            const auto brush = CreateSolidBrush(color);
            const auto tick = [&](int position, bool endpoint) {
                const auto g =
                    ui::DialogTrackbarGeometry(option.rect.width(), option.rect.height(), true,
                                               option.minimum, option.maximum, position);
                const int x = g.thumb.x + g.thumb.width / 2, y = g.thumb.y + g.thumb.height + 1;
                const auto r = rectangle({x, y, 1, endpoint ? 4 : 3});
                FillRect(dc, &r, brush);
            };
            tick(option.minimum, true);
            tick(option.maximum, true);
            const auto interval = option.tickInterval > 0 ? option.tickInterval : option.pageStep;
            if (interval > 0)
                for (auto value = std::int64_t(option.minimum) + interval; value < option.maximum;
                     value += interval)
                    tick(int(value), false);
            DeleteObject(brush);
        }
        // TBS_NOTICKS suppresses tick marks but retains the downward-pointing
        // horizontal thumb. Qt's NoTicks normally selects a rectangular thumb.
        painted = painted && SUCCEEDED(DrawThemeBackground(theme, dc, TKP_THUMBBOTTOM, state,
                                                           &thumb, nullptr));
        GdiFlush();
        if (painted) {
            image.setDevicePixelRatio(ratio);
            painter.drawImage(option.rect.topLeft(), image);
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
    }
    if (dc)
        DeleteDC(dc);
    CloseThemeData(theme);
    return painted;
}
void ConfigureWindowsListPalette(QPalette& palette) {
    for (const auto role : {QPalette::Highlight, QPalette::HighlightedText}) {
        const auto c =
            GetSysColor(role == QPalette::Highlight ? COLOR_HIGHLIGHT : COLOR_HIGHLIGHTTEXT);
        palette.setColor(role, QColor(GetRValue(c), GetGValue(c), GetBValue(c)));
    }
}
void DrawWindowsWindowBorder(QPainter& painter, const QRect& rectangle) {
    const auto color = GetSysColor(COLOR_WINDOWFRAME);
    const QColor border(GetRValue(color), GetGValue(color), GetBValue(color));
    painter.fillRect(QRect(rectangle.left(), rectangle.top(), rectangle.width(), 1), border);
    painter.fillRect(QRect(rectangle.left(), rectangle.bottom(), rectangle.width(), 1), border);
    painter.fillRect(QRect(rectangle.left(), rectangle.top(), 1, rectangle.height()), border);
    painter.fillRect(QRect(rectangle.right(), rectangle.top(), 1, rectangle.height()), border);
}
void DrawWindowsSunkenBorder(QPainter& painter, const QRect& rectangle) {
    const auto line = [&](QRect r, int color) {
        const auto c = GetSysColor(color);
        painter.fillRect(r, QColor(GetRValue(c), GetGValue(c), GetBValue(c)));
    };
    auto edge = rectangle;
    for (unsigned n = 0; n < 2; ++n) {
        // USER32's unthemed listbox uses EDGE_SUNKEN, including while disabled.
        const auto bottom = n ? COLOR_3DLIGHT : COLOR_3DHIGHLIGHT;
        const auto top = n ? COLOR_3DDKSHADOW : COLOR_3DSHADOW;
        line(QRect(edge.left(), edge.bottom(), edge.width(), 1), bottom);
        line(QRect(edge.right(), edge.top(), 1, edge.height()), bottom);
        line(QRect(edge.left(), edge.top(), edge.width() - 1, 1), top);
        line(QRect(edge.left(), edge.top(), 1, edge.height() - 1), top);
        edge.adjust(1, 1, -1, -1);
    }
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
