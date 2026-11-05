#include "dialog_static_label.hpp"
#ifdef _WIN32
#include "editor_style_windows.hpp"
#endif
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QResizeEvent>
#include <climits>

namespace mikudancestudio::qt {
DialogStaticLabel::DialogStaticLabel(const QString& text, std::uint32_t style, QWidget* parent)
    : QLabel(text, parent), nativeStyle_(style) {
    const auto kind = style & 0x1fu;
    setAlignment(Qt::AlignTop | (kind == 1   ? Qt::AlignHCenter
                                 : kind == 2 ? Qt::AlignRight
                                             : Qt::AlignLeft));
    setTextFormat(Qt::PlainText);
    setWordWrap(kind == 0 || kind == 1 || kind == 2);
}
bool DialogStaticLabel::event(QEvent* event) {
    const auto handled = QLabel::event(event);
    if (!fitting_ &&
        (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange ||
         event->type() == QEvent::Move)) {
        FitPortableFont();
    }
    return handled;
}
void DialogStaticLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    FitPortableFont();
}
void DialogStaticLabel::FitPortableFont() {
    if (fitting_ || QGuiApplication::platformName() == "windows" || !wordWrap() ||
        contentsRect().isEmpty() || text().isEmpty())
        return;
    // Some original STATIC rectangles extend beyond their parent dialog. A
    // narrower open font can put another word on that first line, where the
    // parent's clip then cuts it off. Wrap inside the actual ancestor client
    // area while preserving the original child rectangle.
    auto visible = rect();
    for (auto* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        visible &= QRect(mapFrom(ancestor, QPoint()), ancestor->size());
        if (ancestor->isWindow())
            break;
    }
    if (visible.isEmpty())
        return;
    const QMargins margins(visible.left(), visible.top(), width() - visible.right() - 1,
                           height() - visible.bottom() - 1);
    fitting_ = true;
    if (contentsMargins() != margins)
        setContentsMargins(margins);
    fitting_ = false;
    const auto bounds = contentsRect();
    const auto inherited = parentWidget() ? parentWidget()->font() : font();
    auto fitted = inherited;
    // A portable CJK font has full-width kana where MS UI Gothic is
    // proportional. Retain the original row height, control bounds and text;
    // reduce glyph width only if wrapping would lose a row in that rectangle.
    // Starting from the inherited font each time also restores normal width
    // when a label is widened or the language font changes.
    const int originalStretch = inherited.stretch() ? inherited.stretch() : 100;
    const int flags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop;
    for (int stretch = originalStretch; stretch >= originalStretch * 2 / 5; --stretch) {
        fitted.setStretch(stretch);
        const auto extent = QFontMetrics(fitted).boundingRect(
            QRect(0, 0, bounds.width(), INT_MAX / 4), flags, text());
        if (extent.height() <= bounds.height() && extent.width() <= bounds.width())
            break;
    }
    fitting_ = true;
    // Override only width. Copying the resolved inherited font here would
    // freeze its family/size and stop later language-font propagation.
    QFont widthOnly;
    widthOnly.setStretch(fitted.stretch());
    setFont(widthOnly);
    fitting_ = false;
    fittedText_ = text();
}
void DialogStaticLabel::paintEvent(QPaintEvent* event) {
    if (fittedText_ != text())
        FitPortableFont(); // Production physics labels can change their caption.
#ifdef _WIN32
    if (QGuiApplication::platformName() == "windows") {
        QPainter painter(this);
        if (DrawWindowsStaticText(painter, *this, nativeStyle_))
            return;
    }
#endif
    QLabel::paintEvent(event);
}
}
