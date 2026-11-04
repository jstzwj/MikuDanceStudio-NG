#include "editor_style_windows.hpp"
#include "mikudancestudio/mmd_trackbar_geometry.hpp"
#include <QImage>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QStyleOptionButton>
#include <QFontInfo>
#include <QWidget>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QScrollBar>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <cstring>
#include <algorithm>

namespace mikudancestudio::qt {
namespace {
void ControlBackground(QImage& image, const QPalette& palette, qreal ratio, const QWidget* widget) {
    image.setDevicePixelRatio(ratio);
    image.fill(palette.window().color());
    if (widget && widget->parentWidget() &&
        widget->parentWidget()->property("mmdEditorCanvas").toBool()) {
        // Render only our canvas background, never its children or the screen.
        // Native controls inherit that panel gradient instead of an opaque
        // default dialog rectangle around the thumb/button corners.
        QPainter painter(&image);
        widget->parentWidget()->render(&painter, {}, QRegion(widget->geometry()),
                                       QWidget::DrawWindowBackground);
    }
}
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
void DrawWindowsMultilineBackground(QPainter& painter, const QRect& rect,
                                    const QPlainTextEdit& edit) {
    const auto face = GetSysColor(COLOR_BTNFACE), white = GetSysColor(COLOR_WINDOW);
    const auto color = edit.isEnabled() && !edit.isReadOnly() ? white : face;
    painter.fillRect(rect.adjusted(2, 2, -2, -2),
                     QColor(GetRValue(color), GetGValue(color), GetBValue(color)));
}
bool DrawWindowsMultilineText(QPainter& painter, const QPlainTextEdit& edit,
                              const WindowsMultilineLayout* layout) {
    if (edit.property("_mmdImePreedit").toBool())
        return false;
    for (const auto character : edit.toPlainText())
        if (character.unicode() > 0x7f)
            return false;
    const auto ratio = painter.device()->devicePixelRatioF();
    const auto rect = edit.viewport()->rect();
    WindowsCanvas canvas(qRound(rect.width() * ratio), qRound(rect.height() * ratio));
    if (canvas.image.isNull())
        return false;
    const auto font = edit.font();
    const auto nativeFont = CreateFontW(
        -qRound(QFontInfo(font).pixelSize() * ratio), 0, 0, 0, font.bold() ? FW_BOLD : FW_NORMAL,
        font.italic(), font.underline(), font.strikeOut(), DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
        QFontInfo(font).family().toStdWString().c_str());
    if (!nativeFont)
        return false;
    const auto previous = SelectObject(canvas.dc, nativeFont);
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(nativeFont);
        return false;
    }
    RECT clip{0, 0, canvas.image.width(), canvas.image.height()};
    // Shown EDIT erases its complete client background, including the margin
    // outside EM_GETRECT. WM_PRINTCLIENT alone leaves that margin untouched.
    FillRect(
        canvas.dc, &clip,
        GetSysColorBrush(edit.isEnabled() && !edit.isReadOnly() ? COLOR_WINDOW : COLOR_BTNFACE));
    SetBkMode(canvas.dc, TRANSPARENT);
    const auto background =
        GetSysColor(edit.isEnabled() && !edit.isReadOnly() ? COLOR_WINDOW : COLOR_BTNFACE);
    SetBkColor(canvas.dc, background);
    SetTextColor(canvas.dc, GetSysColor(edit.isEnabled() ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
    // The right document margin lies outside EDIT's formatting rectangle;
    // retain left glyph overhang but clip the last partial glyph there.
    clip.right -= std::max(1, qRound(ratio));
    clip.bottom -= std::max(1, qRound(ratio));
    if (edit.horizontalScrollBar()->value())
        clip.left += std::max(1, qRound(ratio));
    for (auto block = edit.document()->begin(); block.isValid(); block = block.next()) {
        const auto text = block.text();
        const auto selected = edit.textCursor();
        const bool focused = layout ? layout->focused : edit.hasFocus();
        const int selectionFirst =
            std::clamp(selected.selectionStart() - block.position(), 0, int(text.size()));
        const int selectionLast =
            std::clamp(selected.selectionEnd() - block.position(), 0, int(text.size()));
        RECT selection{};
        const bool highlight = focused && selectionLast > selectionFirst;
        if (highlight) {
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + selectionFirst);
            const auto first = edit.cursorRect(cursor);
            cursor.setPosition(block.position() + selectionLast);
            const auto last = edit.cursorRect(cursor);
            selection = {qRound(first.x() * ratio), qRound(first.y() * ratio),
                         qRound((last.x() + 1) * ratio),
                         qRound((first.y() + first.height()) * ratio)};
            IntersectRect(&selection, &selection, &clip);
        }
        // Draw selected runs on a fresh highlight background, not over the
        // normal glyph's antialiasing fringe.
        const auto drawRuns = [&](const RECT& bounds) {
            for (int first = 0; first < text.size();) {
                const auto tab = text.indexOf('\t', first);
                const auto last = tab < 0 ? text.size() : tab;
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + first);
                const auto origin = edit.cursorRect(cursor).topLeft();
                const auto caption = text.mid(first, last - first).toStdWString();
                ExtTextOutW(canvas.dc, qRound(origin.x() * ratio), qRound(origin.y() * ratio),
                            ETO_CLIPPED, &bounds, caption.c_str(), UINT(caption.size()), nullptr);
                first = int(last) + 1;
            }
        };
        drawRuns(clip);
        if (highlight) {
            FillRect(canvas.dc, &selection, GetSysColorBrush(COLOR_HIGHLIGHT));
            SetTextColor(canvas.dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            SetBkColor(canvas.dc, GetSysColor(COLOR_HIGHLIGHT));
            drawRuns(selection);
            SetTextColor(canvas.dc,
                         GetSysColor(edit.isEnabled() ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
            SetBkColor(canvas.dc, background);
        }
    }
    if (layout && layout->focused && layout->caretVisible) {
        const auto caret = edit.cursorRect();
        const int saved = SaveDC(canvas.dc);
        IntersectClipRect(canvas.dc, clip.left, clip.top, clip.right, clip.bottom);
        PatBlt(canvas.dc, qRound(caret.x() * ratio), qRound(caret.y() * ratio),
               std::max(1, qRound(ratio)), qRound(caret.height() * ratio), DSTINVERT);
        RestoreDC(canvas.dc, saved);
    }
    GdiFlush();
    canvas.image.setDevicePixelRatio(ratio);
    painter.drawImage(rect.topLeft(), canvas.image);
    SelectObject(canvas.dc, previous);
    DeleteObject(nativeFont);
    return true;
}
bool DrawWindowsEditText(QPainter& painter, const QRect& rect, const QFont& font,
                         const QString& text, bool english, bool border, bool enabled,
                         bool readOnly, bool autoScroll, const WindowsEditLayout* layout) {
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
    // Keep Qt's actual horizontal layout/scroll when supplied. Legacy callers
    // without that state can paint only fitting/unscrolled text.
    const bool painted = layout || !autoScroll || extent.cx <= formatting.right - formatting.left;
    if (painted) {
        RECT bounds{0, 0, canvas.image.width(), canvas.image.height()};
        FillRect(canvas.dc, &bounds,
                 GetSysColorBrush(enabled && !readOnly ? COLOR_WINDOW : COLOR_BTNFACE));
        SetBkMode(canvas.dc, TRANSPARENT);
        SetBkColor(canvas.dc, GetSysColor(enabled && !readOnly ? COLOR_WINDOW : COLOR_BTNFACE));
        SetTextColor(canvas.dc, GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
        const int edge = qRound(inset * ratio);
        RECT clip{edge, edge, canvas.image.width() - edge, canvas.image.height() - edge};
        int textLeft = formatting.left;
        if (layout && autoScroll) {
            SIZE prefix{};
            GetTextExtentPoint32W(canvas.dc, caption.c_str(),
                                  std::clamp(layout->cursor, 0, int(caption.size())), &prefix);
            textLeft = qRound(layout->cursorX * ratio) - prefix.cx;
            if (textLeft < formatting.left)
                clip.left = formatting.left;
        }
        ExtTextOutW(canvas.dc, textLeft, formatting.top, ETO_CLIPPED, &clip, caption.c_str(),
                    UINT(caption.size()), nullptr);
        if (layout && layout->focused && layout->selectionStart >= 0 && layout->selectionLength) {
            const int first = std::clamp(layout->selectionStart, 0, int(caption.size()));
            const int last =
                std::clamp(first + layout->selectionLength, first, int(caption.size()));
            SIZE leading{}, trailing{};
            GetTextExtentPoint32W(canvas.dc, caption.c_str(), first, &leading);
            GetTextExtentPoint32W(canvas.dc, caption.c_str(), last, &trailing);
            RECT selection{textLeft + leading.cx, formatting.top,
                           textLeft + trailing.cx + std::max(1, qRound(ratio)),
                           formatting.top + metrics.tmHeight};
            IntersectRect(&selection, &selection, &clip);
            FillRect(canvas.dc, &selection, GetSysColorBrush(COLOR_HIGHLIGHT));
            SetTextColor(canvas.dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            SetBkColor(canvas.dc, GetSysColor(COLOR_HIGHLIGHT));
            ExtTextOutW(canvas.dc, textLeft, formatting.top, ETO_CLIPPED, &selection,
                        caption.c_str(), UINT(caption.size()), nullptr);
        }
        if (layout && layout->focused && layout->caretVisible) {
            SIZE prefix{};
            GetTextExtentPoint32W(canvas.dc, caption.c_str(),
                                  std::clamp(layout->cursor, 0, int(caption.size())), &prefix);
            const int caret = std::clamp(textLeft + prefix.cx, formatting.left,
                                         std::max(formatting.left, formatting.right - 1));
            const int saved = SaveDC(canvas.dc);
            IntersectClipRect(canvas.dc, clip.left, clip.top, clip.right, clip.bottom);
            // Native EDIT uses an inverting caret. On a selected blue run it
            // is orange, rather than Qt's unconditional black rectangle.
            PatBlt(canvas.dc, caret, formatting.top, std::max(1, qRound(ratio)), metrics.tmHeight,
                   DSTINVERT);
            RestoreDC(canvas.dc, saved);
        }
        GdiFlush();
        canvas.image.setDevicePixelRatio(ratio);
        painter.drawImage(rect.topLeft(), canvas.image);
    }
    SelectObject(canvas.dc, previous);
    DeleteObject(nativeFont);
    return painted;
}
namespace {
bool DrawControlBorder(QPainter& painter, const QRect& rect, QStyle::State state,
                       const wchar_t* themeClass, int part) {
    const auto ratio = painter.device()->devicePixelRatioF();
    const auto theme = OpenThemeDataForDpi(nullptr, themeClass, UINT(qRound(96 * ratio)));
    if (!theme)
        return false;
    const int partState = !(state & QStyle::State_Enabled) ? LBPSN_DISABLED
                          : state & QStyle::State_HasFocus ? LBPSN_FOCUSED
                                                           : LBPSN_NORMAL;
    COLORREF color{};
    const bool painted = SUCCEEDED(GetThemeColor(theme, part, partState, TMT_BORDERCOLOR, &color));
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
}
bool DrawWindowsListBorder(QPainter& painter, const QRect& rect, QStyle::State state) {
    return DrawControlBorder(painter, rect, state, L"ListBox", LBCP_BORDER_NOSCROLL);
}
bool DrawWindowsEditBorder(QPainter& painter, const QRect& rect, QStyle::State state) {
    const auto ratio = painter.device()->devicePixelRatioF();
    WindowsCanvas canvas(qRound(rect.width() * ratio), qRound(rect.height() * ratio));
    if (canvas.image.isNull())
        return false;
    const auto theme = OpenThemeDataForDpi(nullptr, L"Edit", UINT(qRound(96 * ratio)));
    if (!theme)
        return false;
    const int partState = !(state & QStyle::State_Enabled)  ? EPSN_DISABLED
                          : state & QStyle::State_HasFocus  ? EPSN_FOCUSED
                          : state & QStyle::State_MouseOver ? EPSN_HOT
                                                            : EPSN_NORMAL;
    // Rounded theme pixels blend with the dialog, not the EDIT's white client.
    // USER32 first paints the parent background behind this nonclient image.
    const auto parent = GetSysColor(COLOR_BTNFACE);
    canvas.image.fill(QColor(GetRValue(parent), GetGValue(parent), GetBValue(parent)));
    const RECT bounds{0, 0, canvas.image.width(), canvas.image.height()};
    // Modern EDIT borders are theme images. TMT_BORDERCOLOR is an older
    // fallback property and can return the same Aero color for every state,
    // even when USER32 draws different normal, hot and disabled borders.
    const bool painted = SUCCEEDED(
        DrawThemeBackground(theme, canvas.dc, EP_EDITBORDER_NOSCROLL, partState, &bounds, nullptr));
    GdiFlush();
    CloseThemeData(theme);
    if (painted) {
        canvas.image.setDevicePixelRatio(ratio);
        painter.save();
        painter.setClipRegion(QRegion(rect).subtracted(QRegion(rect.adjusted(2, 2, -2, -2))),
                              Qt::IntersectClip);
        painter.drawImage(rect.topLeft(), canvas.image);
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
                       const QRect* indicator, bool radio, bool showMnemonic,
                       const QWidget* widget) {
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
        ControlBackground(image, option.palette, ratio, widget);
        RECT rectangle{0, 0, width, height};
        // A checked BS_PUSHLIKE retains its pressed theme even when disabled.
        // Its input remains disabled; only the native visual state has priority.
        int state = option.state & QStyle::State_On                       ? PBS_PRESSED
                    : !(option.state & QStyle::State_Enabled)             ? PBS_DISABLED
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
                         const ui::TrackbarGeometry& geometry, const QWidget* widget) {
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
        ControlBackground(image, option.palette, ratio, widget);
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
        const bool vertical = option.orientation == Qt::Vertical;
        painted = SUCCEEDED(DrawThemeBackground(theme, dc, vertical ? TKP_TRACKVERT : TKP_TRACK,
                                                TRS_NORMAL, &channel, nullptr));
        if (option.tickPosition != QSlider::NoTicks) {
            COLORREF color = GetSysColor(COLOR_3DSHADOW);
            GetThemeColor(theme, TKP_TICS, TSS_NORMAL, TMT_COLOR, &color);
            const auto brush = CreateSolidBrush(color);
            const auto tick = [&](int position, bool endpoint) {
                const auto g = ui::DialogTrackbarGeometry(
                    vertical ? option.rect.height() : option.rect.width(),
                    vertical ? option.rect.width() : option.rect.height(), true, option.minimum,
                    option.maximum, position);
                const int x = g.thumb.x + g.thumb.width / 2, y = g.thumb.y + g.thumb.height + 1;
                const auto r =
                    rectangle(vertical ? ui::TrackbarRectangle{y, x, endpoint ? 4 : 3, 1}
                                       : ui::TrackbarRectangle{x, y, 1, endpoint ? 4 : 3});
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
        painted =
            painted &&
            SUCCEEDED(DrawThemeBackground(theme, dc, vertical ? TKP_THUMBRIGHT : TKP_THUMBBOTTOM,
                                          state, &thumb, nullptr));
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
