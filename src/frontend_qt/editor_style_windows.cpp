#include "editor_style_windows.hpp"
#include "mikudancestudio/mmd_trackbar_geometry.hpp"
#include <QImage>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QStyleOptionButton>
#include <QFontInfo>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <cstring>

namespace mikudancestudio::qt {
namespace {
class WindowsCanvas {
public:
    HDC dc = nullptr;
    QImage image;
    WindowsCanvas(int width, int height) {
        if (width <= 0 || height <= 0 || !(dc = CreateCompatibleDC(nullptr)))
            return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        bitmap_ = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap_ || !bits)
            return;
        previous_ = SelectObject(dc, bitmap_);
        if (!previous_ || previous_ == HGDI_ERROR) {
            previous_ = nullptr;
            return;
        }
        image = QImage(static_cast<unsigned char*>(bits), width, height, width * 4,
                       QImage::Format_RGB32);
    }
    ~WindowsCanvas() {
        if (previous_)
            SelectObject(dc, previous_);
        if (bitmap_)
            DeleteObject(bitmap_);
        if (dc)
            DeleteDC(dc);
    }
    WindowsCanvas(const WindowsCanvas&) = delete;
    WindowsCanvas& operator=(const WindowsCanvas&) = delete;

private:
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};
}
bool DrawWindowsEditText(QPainter& painter, const QRect& rect, const QFont& font,
                         const QString& text, bool english, bool border, bool enabled,
                         bool readOnly, bool autoScroll) {
    const auto ratio = painter.device()->devicePixelRatioF();
    WindowsCanvas canvas(qRound(rect.width() * ratio), qRound(rect.height() * ratio));
    if (canvas.image.isNull())
        return false;
    const auto family = QFontInfo(font).family().toStdWString();
    const auto nativeFont = CreateFontW(
        -qRound(QFontInfo(font).pixelSize() * ratio), 0, 0, 0, font.bold() ? FW_BOLD : FW_NORMAL,
        font.italic(), font.underline(), font.strikeOut(), DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, family.c_str());
    if (!nativeFont)
        return false;
    const auto previous = SelectObject(canvas.dc, nativeFont);
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(nativeFont);
        return false;
    }
    TEXTMETRICW metrics{};
    const auto caption = text.toStdWString();
    SIZE extent{};
    if (!GetTextMetricsW(canvas.dc, &metrics) ||
        !GetTextExtentPoint32W(canvas.dc, caption.c_str(), int(caption.size()), &extent)) {
        SelectObject(canvas.dc, previous);
        DeleteObject(nativeFont);
        return false;
    }
    // These are the original dialog EDIT's EM_GETRECT / EM_GETMARGINS,
    // independently qualified against Arial/Tahoma and MS PGothic. Native
    // single-line EDITs align to the top, rather than Qt's vertical center.
    const auto inset = border ? 3 : 0; // two client-edge pixels plus EDIT's inner pixel
    const int left = qRound((inset + (english ? 3 : 1)) * ratio);
    const int top = qRound(inset * ratio);
    RECT formatting{left, top, canvas.image.width() - left,
                    std::min(canvas.image.height() - top, top + int(metrics.tmHeight))};
    // Scrolled edits retain Qt's own layout/offset until that state is
    // separately qualified; this path changes only inactive unscrolled text.
    const bool painted = !autoScroll || extent.cx <= formatting.right - formatting.left;
    if (painted) {
        RECT bounds{0, 0, canvas.image.width(), canvas.image.height()};
        FillRect(canvas.dc, &bounds,
                 GetSysColorBrush(enabled && !readOnly ? COLOR_WINDOW : COLOR_BTNFACE));
        SetBkMode(canvas.dc, TRANSPARENT);
        SetTextColor(canvas.dc, GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
        const int edge = qRound(inset * ratio);
        const RECT clip{edge, edge, canvas.image.width() - edge, canvas.image.height() - edge};
        ExtTextOutW(canvas.dc, formatting.left, formatting.top, ETO_CLIPPED, &clip, caption.c_str(),
                    UINT(caption.size()), nullptr);
        GdiFlush();
        canvas.image.setDevicePixelRatio(ratio);
        painter.drawImage(rect.topLeft(), canvas.image);
    }
    SelectObject(canvas.dc, previous);
    DeleteObject(nativeFont);
    return painted;
}
bool DrawWindowsListBorder(QPainter& painter, const QRect& rect, QStyle::State state) {
    const auto ratio = painter.device()->devicePixelRatioF();
    const auto theme = OpenThemeDataForDpi(nullptr, L"ListBox", UINT(qRound(96 * ratio)));
    if (!theme)
        return false;
    const int partState = !(state & QStyle::State_Enabled) ? LBPSN_DISABLED
                          : state & QStyle::State_HasFocus ? LBPSN_FOCUSED : LBPSN_NORMAL;
    COLORREF color{};
    const bool painted = SUCCEEDED(GetThemeColor(theme, LBCP_BORDER_NOSCROLL, partState,
                                                 TMT_BORDERCOLOR, &color));
    CloseThemeData(theme);
    if (painted) {
        // USER32's nonclient edge uses the theme color, not the client part's
        // background bitmap. Preserve the white inner client-edge inset.
        painter.save();
        painter.setClipRegion(QRegion(rect).subtracted(QRegion(rect.adjusted(2, 2, -2, -2))),
                              Qt::IntersectClip);
        painter.fillRect(rect, Qt::white);
        const QColor border(GetRValue(color), GetGValue(color), GetBValue(color));
        painter.fillRect(QRect(rect.left(), rect.top(), rect.width(), 1), border);
        painter.fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), border);
        painter.fillRect(QRect(rect.left(), rect.top(), 1, rect.height()), border);
        painter.fillRect(QRect(rect.right(), rect.top(), 1, rect.height()), border);
        painter.restore();
    }
    return painted;
}
bool DrawWindowsComboText(QImage& background, const QRect& rect, const QFont& font,
                          const QString& text, bool enabled, bool keyboardFocus) {
    WindowsCanvas canvas(background.width(), background.height());
    if (canvas.image.isNull())
        return false;
    std::memcpy(canvas.image.bits(), background.constBits(), background.sizeInBytes());
    const auto ratio = background.devicePixelRatioF();
    const auto family = QFontInfo(font).family().toStdWString();
    const auto nativeFont = CreateFontW(
        -qRound(QFontInfo(font).pixelSize() * ratio), 0, 0, 0, font.bold() ? FW_BOLD : FW_NORMAL,
        font.italic(), font.underline(), font.strikeOut(), DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, family.c_str());
    if (!nativeFont)
        return false;
    const auto previous = SelectObject(canvas.dc, nativeFont);
    SetBkMode(canvas.dc, TRANSPARENT);
    SetTextColor(canvas.dc, GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
    RECT rectangle{qRound(rect.left() * ratio), qRound(rect.top() * ratio),
                   qRound((rect.right() + 1) * ratio), qRound((rect.bottom() + 1) * ratio)};
    const auto focus = rectangle;
    rectangle.left = qRound((rect.left() + 1) * ratio);
    const auto caption = text.toStdWString();
    DrawTextW(canvas.dc, caption.c_str(), int(caption.size()), &rectangle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (keyboardFocus) {
        SetTextColor(canvas.dc, RGB(0, 0, 0));
        SetBkColor(canvas.dc, RGB(255, 255, 255));
        DrawFocusRect(canvas.dc, &focus);
    }
    GdiFlush();
    std::memcpy(background.bits(), canvas.image.constBits(), background.sizeInBytes());
    SelectObject(canvas.dc, previous);
    DeleteObject(nativeFont);
    return true;
}
bool DrawWindowsListRow(QPainter& painter, const QRect& rect, const QFont& font,
                        const QString& text, bool selected, bool enabled, bool keyboardFocus) {
    const auto ratio = painter.device()->devicePixelRatioF();
    const int width = qRound(rect.width() * ratio), height = qRound(rect.height() * ratio);
    if (width <= 0 || height <= 0)
        return false;
    const auto dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const auto nativeFont = CreateFontW(
        -qRound(QFontInfo(font).pixelSize() * ratio), 0, 0, 0, font.bold() ? FW_BOLD : FW_NORMAL,
        font.italic(), font.underline(), font.strikeOut(), DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
        QFontInfo(font).family().toStdWString().c_str());
    bool painted = bitmap && bits && nativeFont;
    if (painted) {
        const auto oldBitmap = SelectObject(dc, bitmap), oldFont = SelectObject(dc, nativeFont);
        RECT rectangle{0, 0, width, height};
        FillRect(dc, &rectangle, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetBkMode(dc, TRANSPARENT);
        // Native LISTBOX keeps the highlight background while disabled, but
        // changes even selected text to COLOR_GRAYTEXT.
        SetTextColor(dc, GetSysColor(!enabled   ? COLOR_GRAYTEXT
                                     : selected ? COLOR_HIGHLIGHTTEXT
                                                : COLOR_WINDOWTEXT));
        const auto caption = text.toStdWString();
        rectangle.left = qRound(2 * ratio);
        // Two client pixels precede the text; ClearType overhang may extend
        // back into that margin. BUTTON's measured-label clipping is different.
        DrawTextW(dc, caption.c_str(), int(caption.size()), &rectangle,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        if (keyboardFocus) {
            // LISTBOX inverts alternate pixels around the complete current
            // item, including text at the top edge. Keep the native XOR phase
            // and colors rather than painting a Qt focus panel over the row.
            RECT focus{0, 0, width, height};
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkColor(dc, RGB(255, 255, 255));
            DrawFocusRect(dc, &focus);
        }
        GdiFlush();
        QImage image(static_cast<unsigned char*>(bits), width, height, width * 4,
                     QImage::Format_RGB32);
        image.setDevicePixelRatio(ratio);
        painter.drawImage(rect.topLeft(), image);
        SelectObject(dc, oldFont);
        SelectObject(dc, oldBitmap);
    }
    if (nativeFont)
        DeleteObject(nativeFont);
    if (bitmap)
        DeleteObject(bitmap);
    DeleteDC(dc);
    return painted;
}
bool DrawWindowsButton(QPainter& painter, const QStyleOptionButton& option, const QFont& font,
                       const QRect* indicator, bool radio, bool showMnemonic) {
    const auto ratio = painter.device()->devicePixelRatioF();
    const int width = qRound(option.rect.width() * ratio),
              height = qRound(option.rect.height() * ratio);
    const auto theme = OpenThemeDataForDpi(nullptr, L"Button", UINT(qRound(96 * ratio)));
    if (!theme || width <= 0 || height <= 0) {
        if (theme)
            CloseThemeData(theme);
        return false;
    }
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
        RECT rectangle{0, 0, width, height};
        int state = !(option.state & QStyle::State_Enabled)               ? PBS_DISABLED
                    : option.state & QStyle::State_Sunken                 ? PBS_PRESSED
                    : option.state & QStyle::State_MouseOver              ? PBS_HOT
                    : option.features & QStyleOptionButton::DefaultButton ? PBS_DEFAULTED
                                                                          : PBS_NORMAL;
        int part = BP_PUSHBUTTON;
        RECT textRectangle = rectangle;
        if (indicator) {
            part = radio ? BP_RADIOBUTTON : BP_CHECKBOX;
            state = !(option.state & QStyle::State_Enabled)  ? 4
                    : option.state & QStyle::State_Sunken    ? 3
                    : option.state & QStyle::State_MouseOver ? 2
                                                             : 1;
            state += option.state & QStyle::State_NoChange ? 8
                     : option.state & QStyle::State_On     ? 4
                                                           : 0;
            RECT mark{qRound(indicator->left() * ratio), qRound(indicator->top() * ratio),
                      qRound((indicator->right() + 1) * ratio),
                      qRound((indicator->bottom() + 1) * ratio)};
            painted = SUCCEEDED(DrawThemeBackground(theme, dc, part, state, &mark, nullptr));
            textRectangle.left = qRound((indicator->right() + 1 + 3) * ratio);
        } else
            painted = SUCCEEDED(DrawThemeBackground(theme, dc, part, state, &rectangle, nullptr));
        const auto family = QFontInfo(font).family().toStdWString();
        const auto nativeFont =
            CreateFontW(-qRound(QFontInfo(font).pixelSize() * ratio), 0, 0, 0,
                        font.bold() ? FW_BOLD : FW_NORMAL, font.italic(), font.underline(),
                        font.strikeOut(), DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, DEFAULT_PITCH, family.c_str());
        if (nativeFont) {
            const auto oldFont = SelectObject(dc, nativeFont);
            const auto text = option.text.toStdWString();
            SetBkMode(dc, TRANSPARENT);
            const auto background = option.palette.window().color();
            SetBkColor(dc, RGB(background.red(), background.green(), background.blue()));
            RECT content{};
            const auto saved = SaveDC(dc);
            if (!indicator && SUCCEEDED(GetThemeBackgroundContentRect(theme, dc, part, state,
                                                                      &rectangle, &content))) {
                IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);
                textRectangle = content;
            }
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc, &metrics);
            const auto prefix = showMnemonic ? 0u : DT_HIDEPREFIX;
            if (indicator) {
                // Native zero-leading cells round their centered text upward;
                // cells with internal leading use integer truncation. Short
                // controls retain the theme mark's minimum label height.
                // Keep the native measured text bounds:
                // ClearType overhang outside that box is clipped by BUTTON.
                textRectangle.top = (height - metrics.tmHeight) / 2;
                if (!metrics.tmInternalLeading && height >= metrics.tmHeight)
                    textRectangle.top = (height - metrics.tmHeight + 1) / 2;
                SIZE markSize{};
                if (SUCCEEDED(
                        GetThemePartSize(theme, dc, part, state, nullptr, TS_TRUE, &markSize)) &&
                    height >= metrics.tmHeight && height <= markSize.cy)
                    textRectangle.top = (markSize.cy - metrics.tmHeight + 1) / 2;
                textRectangle.bottom = textRectangle.top + metrics.tmHeight;
                auto measured = textRectangle;
                DrawTextW(dc, text.c_str(), int(text.size()), &measured,
                          DT_LEFT | DT_SINGLELINE | prefix | DT_CALCRECT);
                textRectangle.right = std::min(textRectangle.right, measured.right);
            }
            COLORREF textColor =
                GetSysColor(option.state & QStyle::State_Enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
            GetThemeColor(theme, part, state, TMT_TEXTCOLOR, &textColor);
            SetTextColor(dc, textColor);
            DrawTextW(dc, text.c_str(), int(text.size()), &textRectangle,
                      (indicator ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | prefix);
            if ((option.state & QStyle::State_HasFocus) &&
                (option.state & QStyle::State_KeyboardFocusChange)) {
                auto focus = textRectangle;
                if (indicator)
                    InflateRect(&focus, 1, 1);
                IntersectRect(&focus, &focus, &rectangle);
                DrawFocusRect(dc, &focus);
            }
            RestoreDC(dc, saved);
            SelectObject(dc, oldFont);
            DeleteObject(nativeFont);
        } else
            painted = false;
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
