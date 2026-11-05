#include "numeric_dialog.hpp"
#include "editor_style.hpp"
#include "editor_font.hpp"
#ifdef _WIN32
#include "editor_style_windows.hpp"
#endif
#include <QApplication>
#include "edit_composition.hpp"
#include "edit_navigation.hpp"
#include "multiline_hit.hpp"
#include "mikudancestudio/mmd_dialog_edit_metrics.hpp"
#include "mikudancestudio/gravity_edit.hpp"
#include <QLabel>
#include <QGroupBox>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyleOptionFrame>
#include <QValidator>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QScrollBar>
#include <QTimer>
#include <charconv>
#include <climits>
#include <cmath>
namespace mikudancestudio::qt {
namespace {
#ifdef _WIN32
class DialogMultilineLayout final : public QPlainTextDocumentLayout {
public:
    explicit DialogMultilineLayout(QTextDocument* document) : QPlainTextDocumentLayout(document) {}
    QRectF blockBoundingRect(const QTextBlock& block) const override {
        auto bounds = QPlainTextDocumentLayout::blockBoundingRect(block);
        if (!block.isValid() || !block.layout()->preeditAreaText().isEmpty())
            return bounds;
        for (const auto c : block.text()) {
            const auto value = c.unicode();
            if (!(value < 128 || (value >= 0x3400 && value <= 0x9fff) ||
                  (value >= 0x3000 && value <= 0x30ff && c.category() != QChar::Mark_NonSpacing &&
                   c.category() != QChar::Mark_SpacingCombining)))
                return bounds;
        }
        // EDIT advances by tmHeight, excluding a fallback font's external
        // leading. Adjust the actual QTextLines, not just the paint rectangle:
        // selection, cursor, hit testing, scrolling and IME share this layout.
        // Complex Unicode/preedit retains Qt's full shaping and spacing.
        qreal top = 0;
        for (int i = 0; i < block.layout()->lineCount(); ++i) {
            auto line = block.layout()->lineAt(i);
            line.setLeadingIncluded(false);
            line.setPosition(QPointF(line.x(), top));
            top += line.height();
        }
        return QPlainTextDocumentLayout::blockBoundingRect(block);
    }
};
#endif
class DialogMultilineEdit final : public QPlainTextEdit {
public:
    DialogMultilineEdit(bool english, bool border, int scrollColumn, QWidget* parent)
        : QPlainTextEdit(parent), scrollColumn_(scrollColumn) {
#ifdef _WIN32
        document()->setDocumentLayout(new DialogMultilineLayout(document()));
#endif
        // Original EM_GETRECT/EM_POSFROMCHAR: four client pixels on the
        // left, one above; the default 32-DLU tabs are 56 logical pixels.
        // Apply them to Qt's actual viewport/document, so drawing, hit testing,
        // caret movement and IME candidate placement share the same origin.
        setFrameStyle(QFrame::NoFrame);
        setProperty("mmdNativeSunkenEdit", border);
        if (border)
            setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        // Keep one pixel of text overhang inside the viewport, rather than
        // clipping a first glyph that extends into EDIT's formatting margin.
        // The document margin preserves the measured caret/line origins.
        document()->setDocumentMargin(1);
        setViewportMargins(3, 0, english ? 3 : 1, 0);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this, english](int value) {
            // QPlainTextEdit applies the document's top margin only at its
            // first block. EDIT retains its formatting inset after scrolling.
            const int top = value ? 1 : 0;
            if (viewportMargins().top() != top)
                setViewportMargins(3, top, english ? 3 : 1, 0);
        });
        setTabStopDistance(56);
#ifdef _WIN32
        connect(&caretTimer_, &QTimer::timeout, this, [this] {
            caretVisible_ = !caretVisible_;
            viewport()->update();
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { ResetCaret(); });
        connect(this, &QPlainTextEdit::selectionChanged, this, [this] { ResetCaret(); });
#endif
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPlainTextEdit::paintEvent(event);
#ifdef _WIN32
        if (QGuiApplication::platformName() == "windows") {
            QPainter painter(viewport());
            const WindowsMultilineLayout layout{hasFocus(), caretVisible_};
            DrawWindowsMultilineText(painter, *this, &layout);
        }
#endif
    }
    void mousePressEvent(QMouseEvent* event) override {
        MouseEvent(event, QEvent::MouseButtonPress);
    }
    void mouseMoveEvent(QMouseEvent* event) override { MouseEvent(event, QEvent::MouseMove); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        MouseEvent(event, QEvent::MouseButtonRelease);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        MouseEvent(event, QEvent::MouseButtonDblClick);
    }
    void MouseEvent(QMouseEvent* event, QEvent::Type type) {
        auto cursor = MultilineCursorForPosition(*this, event->position());
        const auto previous = textCursor();
        const auto rectangle = cursorRect(cursor);
        const auto bounds = viewport()->rect();
        const bool outside = !bounds.contains(event->position().toPoint());
        const int horizontal = horizontalScrollBar()->value();
        const int vertical = verticalScrollBar()->value();
        bool ascii = !property("_mmdImePreedit").toBool();
        for (const auto character : cursor.block().text())
            ascii &= character.unicode() < 128;
        bool legacy = ascii && !(event->modifiers() &
                                 (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        const bool dragging = mouseAnchor_ >= 0 &&
                              ((type == QEvent::MouseMove && event->buttons() & Qt::LeftButton) ||
                               type == QEvent::MouseButtonRelease);
        QTextBlock outsideBlock;
        if (legacy && dragging && outside && lineWrapMode() == QPlainTextEdit::NoWrap) {
            const auto first = firstVisibleBlock();
            const auto client =
                rect().adjusted(frameWidth(), frameWidth(), -frameWidth(), -frameWidth());
            const auto offset = viewport()->pos() - client.topLeft();
            const auto y = std::clamp(event->position().y() + offset.y(), qreal(0),
                                      qreal(client.height() - 1)) -
                           offset.y();
            const auto row = int(
                std::floor((y - cursorRect(QTextCursor(first)).top()) / fontMetrics().height()));
            outsideBlock = document()->findBlockByNumber(
                std::clamp(first.blockNumber() + row, 0, document()->blockCount() - 1));
            for (const auto character : outsideBlock.text())
                legacy &= character.unicode() < 128;
        }
        if (legacy && outsideBlock.isValid()) {
            // Actual EDIT updates only on mouse movement, retaining the column
            // and confining the row to the client edge. Qt's timer selects all the way
            // to the document boundary while the pointer remains stationary.
            // Keep Qt's real OS capture, but do not start that timer for ASCII.
            if (type == QEvent::MouseMove) {
                const auto client =
                    rect().adjusted(frameWidth(), frameWidth(), -frameWidth(), -frameWidth());
                const auto offset = viewport()->pos() - client.topLeft();
                const auto block = outsideBlock;
                const auto origin = cursorRect(QTextCursor(block)).topLeft();
                const auto line = block.layout()->lineAt(0);
                const int target =
                    block.position() + line.xToCursor(event->position().x() - origin.x() +
                                                      line.cursorToX(0) + qreal(1.0 / 64));
                cursor.setPosition(mouseAnchor_);
                cursor.setPosition(target, QTextCursor::KeepAnchor);
                setTextCursor(cursor);
                const int visible = verticalScrollBar()->pageStep();
                verticalScrollBar()->setValue(std::clamp(block.blockNumber() - visible + 1,
                                                         std::min(vertical, block.blockNumber()),
                                                         std::max(vertical, block.blockNumber())));
                horizontalScrollBar()->setValue(horizontal);
                const int x = cursorRect().left() + offset.x();
                const int width = client.width();
                // EDIT advances by a third-client page in whole native scroll
                // columns. Its range retains the final caret pixel too.
                const int step = (width / 3 / scrollColumn_ + 1) * scrollColumn_;
                horizontalScrollBar()->setMaximum(std::max(
                    0, int(std::ceil(document()->size().width())) - viewport()->width() + 1));
                if (x < offset.x() + 1)
                    horizontalScrollBar()->setValue(horizontal - step);
                else if (x > offset.x() + viewport()->width() - 1)
                    horizontalScrollBar()->setValue(horizontal + step);
            } else {
                // EDIT's button-up releases capture without a second hit or
                // scroll. Let Qt release its internal pressed state too.
                const auto selected = textCursor();
                QPlainTextEdit::mouseReleaseEvent(event);
                setTextCursor(selected);
                horizontalScrollBar()->setValue(horizontal);
                verticalScrollBar()->setValue(vertical);
                mouseAnchor_ = -1;
            }
            event->accept();
            return;
        }
        // Qt owns Unicode shaping, IME delivery and capture. Legacy ASCII
        // selection retains its native hit point and directional anchor.
        auto position = event->position();
        if (viewport()->rect().contains(position.toPoint()) &&
            cursor.position() != cursorForPosition(position.toPoint()).position())
            position = QPointF(rectangle.left(), rectangle.center().y());
        QMouseEvent adjusted(type, position, event->scenePosition(), event->globalPosition(),
                             event->button(), event->buttons(), event->modifiers(), event->source(),
                             event->pointingDevice());
        if (type == QEvent::MouseButtonPress)
            QPlainTextEdit::mousePressEvent(&adjusted);
        else if (type == QEvent::MouseMove)
            QPlainTextEdit::mouseMoveEvent(&adjusted);
        else if (type == QEvent::MouseButtonRelease)
            QPlainTextEdit::mouseReleaseEvent(&adjusted);
        else
            QPlainTextEdit::mouseDoubleClickEvent(&adjusted);
        if (legacy) {
            if (type == QEvent::MouseButtonPress && event->button() == Qt::LeftButton) {
                mouseAnchor_ =
                    event->modifiers() & Qt::ShiftModifier ? previous.anchor() : cursor.position();
                auto selected = cursor;
                selected.setPosition(mouseAnchor_);
                selected.setPosition(cursor.position(), QTextCursor::KeepAnchor);
                setTextCursor(selected);
            } else if (dragging) {
                auto selected = cursor;
                selected.setPosition(mouseAnchor_);
                selected.setPosition(cursor.position(), QTextCursor::KeepAnchor);
                setTextCursor(selected);
                if (outside) {
                    horizontalScrollBar()->setValue(horizontal);
                    verticalScrollBar()->setValue(vertical);
                }
            } else if (type == QEvent::MouseButtonDblClick && event->button() == Qt::LeftButton) {
                const auto block = cursor.block();
                const auto end =
                    EditWordPosition(block.text(), cursor.position() - block.position(), true);
                const auto start = EditWordPosition(block.text(), end, false);
                auto selected = cursor;
                selected.setPosition(block.position() + start);
                selected.setPosition(block.position() + end, QTextCursor::KeepAnchor);
                setTextCursor(selected);
                mouseAnchor_ = -1;
            }
        }
        if (type == QEvent::MouseButtonRelease)
            mouseAnchor_ = -1;
        event->setAccepted(adjusted.isAccepted());
    }
    bool event(QEvent* event) override {
        const auto handled = QPlainTextEdit::event(event);
#ifdef _WIN32
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut ||
            event->type() == QEvent::KeyPress || event->type() == QEvent::InputMethod ||
            event->type() == QEvent::MouseButtonPress)
            ResetCaret();
#endif
        return handled;
    }
    void keyPressEvent(QKeyEvent* event) override {
        const auto modifiers = event->modifiers();
        const bool legacy =
            !property("_mmdImePreedit").toBool() &&
            !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        auto cursor = textCursor();
        if (legacy && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) {
            // EDIT pages overlap by one complete text row. Its caret moves by
            // the rows actually scrolled, so PageUp at the first page and
            // PageDown at the last page preserve the caret/selection.
            auto* scroll = verticalScrollBar();
            const int before = scroll->value();
            const int rows = std::max(1, scroll->pageStep() - 1);
            scroll->setValue(before + (event->key() == Qt::Key_PageUp ? -rows : rows));
            const int delta = scroll->value() - before;
            if (delta) {
                cursor.movePosition(delta < 0 ? QTextCursor::Up : QTextCursor::Down,
                                    modifiers & Qt::ShiftModifier ? QTextCursor::KeepAnchor
                                                                  : QTextCursor::MoveAnchor,
                                    std::abs(delta));
                setTextCursor(cursor);
                scroll->setValue(before + delta);
            } else if (!(modifiers & Qt::ShiftModifier) && cursor.hasSelection()) {
                cursor.clearSelection();
                setTextCursor(cursor);
            }
            event->accept();
            return;
        }
        if (legacy && ((event->key() == Qt::Key_Up && cursor.blockNumber() == 0) ||
                       (event->key() == Qt::Key_Down &&
                        cursor.blockNumber() == document()->blockCount() - 1))) {
            // Qt extends to the document boundary when the requested row is
            // absent; the original EDIT retains the active column instead.
            if (!(modifiers & Qt::ShiftModifier) && cursor.hasSelection()) {
                cursor.clearSelection();
                setTextCursor(cursor);
            }
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

private:
    int mouseAnchor_ = -1;
    int scrollColumn_;
#ifdef _WIN32
    QTimer caretTimer_;
    bool caretVisible_ = false;
    void ResetCaret() {
        caretTimer_.stop();
        caretVisible_ = hasFocus();
        if (caretVisible_ && QApplication::cursorFlashTime() > 0)
            caretTimer_.start(std::max(1, QApplication::cursorFlashTime() / 2));
        viewport()->update();
    }
#endif
};
class DialogLineEdit final : public QLineEdit {
    class WidthValidator final : public QValidator {
    public:
        explicit WidthValidator(DialogLineEdit& edit) : QValidator(&edit), edit_(edit) {}
        State validate(QString& text, int& cursor) const override {
            return edit_.LimitInsertion(text, cursor);
        }

    private:
        DialogLineEdit& edit_;
    };

public:
    DialogLineEdit(bool english, bool autoScroll, QWidget* parent)
        : QLineEdit(parent), english_(english), autoScroll_(autoScroll) {
        if (!autoScroll_)
            setValidator(new WidthValidator(*this));
#ifdef _WIN32
        connect(&caretTimer_, &QTimer::timeout, this, [this] {
            caretVisible_ = !caretVisible_;
            update();
        });
        connect(this, &QLineEdit::cursorPositionChanged, this, [this] { ResetCaret(); });
        connect(this, &QLineEdit::selectionChanged, this, [this] { ResetCaret(); });
        connect(this, &QLineEdit::textChanged, this, [this] { ResetCaret(); });
#endif
    }

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
        auto result = QLineEdit::inputMethodQuery(query);
        if (query == Qt::ImCursorRectangle || query == Qt::ImAnchorRectangle) {
            // QWidgetLineControl exposes its ten-pixel repaint halo here.
            // Platform IMEs need the actual one-pixel caret, not its damage
            // region: QWindowsInputContext uses this rectangle verbatim.
            auto cursor = result.toRect();
            cursor.translate(5, 0);
            cursor.setWidth(1);
            result = cursor;
        }
        return result;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        // Keep Qt's text layout, scroll offsets and cursor/IME geometry current
        // even when native EDIT glyphs/selection are painted over its raster below.
        QLineEdit::paintEvent(event);
#ifdef _WIN32
        if (QGuiApplication::platformName() == "windows" && !property("_mmdImePreedit").toBool() &&
            (alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft &&
            echoMode() == QLineEdit::Normal) {
            bool nativeText = true;
            for (const auto c : text()) {
                const auto value = c.unicode();
                nativeText &=
                    (value >= 32 && value <= 126) || (value >= 0x3400 && value <= 0x9fff) ||
                    (value >= 0x3000 && value <= 0x30ff && c.category() != QChar::Mark_NonSpacing &&
                     c.category() != QChar::Mark_SpacingCombining);
            }
            if (nativeText) {
                // The original dialog edits do not set ES_NOHIDESEL. Retain
                // Qt's selection for copy/undo, but hide its inactive highlight.
                QPainter painter(this);
                const WindowsEditLayout layout{
                    cursorPosition(), inputMethodQuery(Qt::ImCursorRectangle).toRect().x(),
                    selectionStart(), int(selectedText().size()),
                    hasFocus(),       caretVisible_};
                if (DrawWindowsEditText(painter, rect(), font(), text(), english_, hasFrame(),
                                        isEnabled(), isReadOnly(), autoScroll_, &layout)) {
                    if (hasFrame()) {
                        QStyleOptionFrame frame;
                        initStyleOption(&frame);
                        style()->drawPrimitive(QStyle::PE_FrameLineEdit, &frame, &painter, this);
                    }
                    return;
                }
            }
        }
#endif
    }
    bool event(QEvent* event) override {
#ifdef _WIN32
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            const auto handled = QLineEdit::event(event);
            ResetCaret();
            return handled;
        }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::InputMethod ||
            event->type() == QEvent::MouseButtonPress)
            ResetCaret();
#endif
        const bool insertion =
            event->type() == QEvent::KeyPress || event->type() == QEvent::InputMethod ||
            event->type() == QEvent::ContextMenu || event->type() == QEvent::Drop;
        if (autoScroll_ || editing_ || !insertion)
            return QLineEdit::event(event);
        before_ = text();
        first_ = hasSelectedText() ? selectionStart() : cursorPosition();
        last_ = first_ + selectedText().size();
        editing_ = true;
        const auto handled = QLineEdit::event(event);
        editing_ = false;
        return handled;
    }

private:
#ifdef _WIN32
    QTimer caretTimer_;
    bool caretVisible_ = false;
    void ResetCaret() {
        caretTimer_.stop();
        caretVisible_ = hasFocus();
        if (caretVisible_ && QApplication::cursorFlashTime() > 0)
            caretTimer_.start(std::max(1, QApplication::cursorFlashTime() / 2));
        update();
    }
#endif
    QValidator::State LimitInsertion(QString& text, int& cursor) const {
        // WM_SETTEXT bypasses this native insertion limit. Preserve Qt's
        // programmatic setter too. Non-ASCII shaping is qualified separately.
        if (!editing_)
            return QValidator::Acceptable;
        for (const auto& value : {before_, text})
            for (const auto c : value)
                if (c.unicode() < 32 || c.unicode() > 126)
                    return QValidator::Acceptable;
        const auto oldBytes = before_.toLatin1();
        if (ui::NumericPrefillLength(std::string_view(oldBytes.constData(), oldBytes.size()),
                                     english_, width()) != std::size_t(oldBytes.size()))
            return QValidator::Acceptable;
        const auto prefix = before_.left(first_), suffix = before_.mid(last_);
        if (!text.startsWith(prefix) || !text.endsWith(suffix) ||
            text.size() < prefix.size() + suffix.size())
            return QValidator::Acceptable; // Delete/backspace/cut, not insertion.
        const auto inserted = text.mid(first_, text.size() - prefix.size() - suffix.size());
        const auto bytes = inserted.toLatin1();
        const auto length = ui::NumericInsertedLength(
            std::string_view(oldBytes.constData(), oldBytes.size()), first_, last_,
            std::string_view(bytes.constData(), bytes.size()), english_, width());
        if (length == std::size_t(bytes.size()))
            return QValidator::Acceptable;
        if (!length && first_ == last_)
            return QValidator::Invalid;
        text = prefix + inserted.left(qsizetype(length)) + suffix;
        cursor = int(first_ + length);
        return QValidator::Acceptable;
    }
    bool english_, autoScroll_, editing_ = false;
    QString before_;
    qsizetype first_ = 0, last_ = 0;
};
class OrderRowDelegate final : public QStyledItemDelegate {
public:
    OrderRowDelegate(int height, QObject* parent) : QStyledItemDelegate(parent), height_(height) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
#ifdef _WIN32
        if (QGuiApplication::platformName() == "windows" &&
            DrawWindowsListRow(
                *painter, option.rect, option.font, index.data(Qt::DisplayRole).toString(),
                option.state & QStyle::State_Selected, option.state & QStyle::State_Enabled,
                // Item-view options carry HasFocus for the current focused
                // item. QCommonStyle adds KeyboardFocusChange only later when
                // constructing its separate focus-rectangle option.
                option.state & QStyle::State_HasFocus))
            return;
#endif
        auto row = option;
        if (row.state & QStyle::State_Selected) {
            // The original LISTBOX retains its flat system highlight even
            // when the dialog is inactive or the control is disabled.
            painter->fillRect(row.rect, row.palette.color(QPalette::Active, QPalette::Highlight));
            row.palette.setColor(QPalette::Text,
                                 row.palette.color(QPalette::Active, QPalette::HighlightedText));
        }
        row.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
        QStyledItemDelegate::paint(painter, row, index);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(height_);
        return size;
    }

private:
    int height_;
};
class DialogTrackbar final : public QSlider {
public:
    explicit DialogTrackbar(QWidget* owner) : QSlider(Qt::Horizontal, owner) {
        setSingleStep(1);
        setPageStep(40);
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const qint64 delta = qint64(wheelRemainder_) + event->angleDelta().y();
        const auto steps = delta / 120;
        wheelRemainder_ = int(delta % 120);
        setValue(int(std::clamp(qint64(value()) - steps * std::max(1, pageStep() / 10),
                                qint64(minimum()), qint64(maximum()))));
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        switch (event->key()) {
        case Qt::Key_Up:
            triggerAction(SliderSingleStepSub);
            break;
        case Qt::Key_Down:
            triggerAction(SliderSingleStepAdd);
            break;
        case Qt::Key_PageUp:
            triggerAction(SliderPageStepSub);
            break;
        case Qt::Key_PageDown:
            triggerAction(SliderPageStepAdd);
            break;
        default:
            QSlider::keyPressEvent(event);
            return;
        }
        event->accept();
    }

private:
    int wheelRemainder_ = 0;
};
bool DoublePrefix(const QString& text, int limit, double& result) {
    const auto bytes = text.toUtf8().left(limit);
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    while (begin < end && (*begin == ' ' || (*begin >= '\t' && *begin <= '\r')))
        ++begin;
    // libc++ on the macOS baseline lacks floating-point from_chars. Keep
    // native atof prefix behavior independent of the user's decimal separator.
    static const QRegularExpression prefix(
        QStringLiteral("^[+-]?(?:(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)"
                       "(?:[eE][+-]?[0-9]+)?|inf(?:inity)?|nan)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = prefix.match(QString::fromLatin1(begin, end - begin));
    double parsed = 0;
    bool valid = true;
    if (match.hasMatch())
        parsed = QLocale::c().toDouble(match.captured(), &valid);
    if (!valid || !std::isfinite(parsed) || !std::isfinite(float(parsed)))
        return false;
    result = parsed;
    return true;
}
bool FloatPrefix(const QString& text, int limit, float& result) {
    double parsed;
    if (!DoublePrefix(text, limit, parsed))
        return false;
    result = float(parsed);
    return true;
}
int EdgePosition(float value) {
    const float scaled = value * 100.f; // Native x64 mulss then cvttss2si.
    if (!std::isfinite(scaled) || scaled >= 2147483648.f || scaled < -2147483648.f)
        return 0;
    return std::clamp(int(scaled), 0, 200);
}
}
bool NativeDialogDoublePrefix(const QString& text, int limit, double& result) {
    return DoublePrefix(text, limit, result);
}
TemplateDialog::TemplateDialog(const ui::NumericTemplate& description, const Language& language,
                               QWidget* parent)
    : QDialog(parent), template_(description), english_(language.Code() == "en") {
    InstallEditorStyle(*qApp);
    setWindowTitle(language.Text(template_.title));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QFont font;
    font.setFamilies(
        language.Code() == "en"
            ? QStringList{QString::fromUtf8(template_.fontFamily), "Liberation Sans", "Helvetica",
                          "DejaVu Sans"}
            : QStringList{QString::fromUtf8(template_.fontFamily),
                          language.Code() == "ja" ? "Noto Sans CJK JP" : "Noto Sans CJK SC",
                          "Hiragino Sans", "Microsoft YaHei", "PingFang SC"});
    font.setPixelSize(template_.pointSize * 96 / 72);
    setFont(MatchEditorFontMetrics(font));
    // Original 96-DPI Arial / MS PGothic dialog base units, independently
    // measured in a memory DC. Qt logical pixels then scale with the screen.
    const int baseX = 7,
              baseY = language.Code() == "en"
                          ? (QString::fromUtf8(template_.fontFamily) == "Tahoma" ? 14 : 15)
                          : 12;
    const auto x = [&](int v) { return (v * baseX + 2) / 4; };
    const auto y = [&](int v) { return (v * baseY + 4) / 8; };
    setFixedSize(x(template_.width), y(template_.height));
    QWidget* previous = nullptr;
    for (std::size_t i = 0; i < template_.count; ++i) {
        const auto& control = template_.controls[i];
        QWidget* widget = nullptr;
        if (control.kind == ui::NumericControlKind::Edit) {
            auto* field = new DialogLineEdit(english_, (control.style & 0x80u) != 0, this);
            field->setReadOnly((control.style & 0x800u) != 0);
            field->setFrame((control.style & 0x00800000u) != 0);
            field->setProperty("mmdNativeSunkenEdit", (control.style & 0x00800000u) != 0);
            // Share the original logical text origin with Qt's cursor, hit
            // testing and IME layout on every platform. EditorStyle supplies
            // the same two-pixel dialog client edge independently of the theme.
            field->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            const int margin =
                (english_ ? 3 : 1) + (field->hasFrame() ? 3 : 0) - (field->hasFrame() ? 2 : 0) - 2;
            // WM_PRINT's actual glyph/selection/caret starts one pixel below
            // EM_POSFROMCHAR. Keep paint, hit testing and IME geometry together.
            // Qt includes an extra trailing caret pixel in naturalTextWidth;
            // native ES_AUTOHSCROLL keeps that pixel inside the formatting box.
            field->setTextMargins(margin, 0, margin - int((control.style & 0x80u) != 0), 0);
            edits_[control.id] = field;
            field->installEventFilter(this);
            widget = field;
        } else if (control.kind == ui::NumericControlKind::Multiline) {
            auto* field =
                new DialogMultilineEdit(english_, (control.style & 0x00800000u) != 0, baseX, this);
            field->setReadOnly((control.style & 0x800u) != 0);
            field->setLineWrapMode(control.style & 0x80u ? QPlainTextEdit::NoWrap
                                                         : QPlainTextEdit::WidgetWidth);
            field->setVerticalScrollBarPolicy(control.style & 0x200000u ? Qt::ScrollBarAsNeeded
                                                                        : Qt::ScrollBarAlwaysOff);
            field->setHorizontalScrollBarPolicy(control.style & 0x100000u ? Qt::ScrollBarAsNeeded
                                                                          : Qt::ScrollBarAlwaysOff);
            field->setTabChangesFocus(true);
            field->setProperty("mmdWantReturn", (control.style & 0x1000u) != 0);
            field->setProperty("mmdMultilineDelCharacter", english_ ? 0x2302 : 0x7f);
            multiline_[control.id] = field;
            field->installEventFilter(this);
            widget = field;
        } else if (control.kind == ui::NumericControlKind::Static) {
            auto* label = new QLabel(language.Text(control.message), this);
            label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            widget = label;
        } else if (control.kind == ui::NumericControlKind::Groupbox) {
            widget = new QGroupBox(language.Text(control.message), this);
        } else if (control.kind == ui::NumericControlKind::Slider) {
            auto* slider = new DialogTrackbar(this);
            slider->setProperty("mmdNativeTrackbar", true);
            slider->setTickPosition(control.style & 0x10u ? QSlider::NoTicks : QSlider::TicksBelow);
            slider->setFocusPolicy(Qt::ClickFocus);
            sliders_[control.id] = slider;
            widget = slider;
        } else if (control.kind == ui::NumericControlKind::Combobox) {
            auto* combo = new QComboBox(this);
            combo->setFocusPolicy(Qt::ClickFocus); // Original 669 has no WS_TABSTOP.
            combos_[control.id] = combo;
            widget = combo;
        } else if (control.kind == ui::NumericControlKind::Listbox) {
            auto* list = new QListWidget(this);
            list->setProperty("mmdNativeSunkenBorder", true);
            auto colors = list->palette();
#ifdef _WIN32
            if (QGuiApplication::platformName() == "windows")
                ConfigureWindowsListPalette(colors);
#endif
            colors.setColor(QPalette::Disabled, QPalette::Base,
                            colors.color(QPalette::Active, QPalette::Base));
            list->setPalette(colors);
            list->setFocusPolicy(Qt::ClickFocus);
            list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            list->setSelectionMode(QAbstractItemView::SingleSelection);
            list->setItemDelegate(new OrderRowDelegate(baseY, list));
            lists_[control.id] = list;
            widget = list;
        } else if (control.kind == ui::NumericControlKind::Checkbox) {
            auto* check = new QCheckBox(language.Text(control.message), this);
            check->setFocusPolicy(Qt::ClickFocus);
            checkboxes_[control.id] = check;
            widget = check;
        } else if (control.kind == ui::NumericControlKind::Radio) {
            auto* radio = new QRadioButton(language.Text(control.message), this);
            radio->setAutoExclusive(false);
            radio->setFocusPolicy(Qt::ClickFocus);
            radios_[control.id] = radio;
            connect(radio, &QRadioButton::clicked, this,
                    [this, id = control.id] { ButtonClicked(id); });
            widget = radio;
        } else {
            auto* button = new QPushButton(language.Text(control.message), this);
            button->setAutoDefault(false);
            button->setDefault(control.id == 1 || control.id == 633);
            connect(button, &QPushButton::clicked, this,
                    [this, id = control.id] { ButtonClicked(id); });
            widget = button;
        }
        widget->setObjectName(QString("numeric.control.%1").arg(i));
        widget->setProperty("mmdControlId", control.id);
        // A Win32 dropdown resource stores the expanded height. The closed
        // control uses font height + 8 pixels (measured on hidden 813/812).
        widget->setGeometry(x(control.x), y(control.y), x(control.width),
                            control.kind == ui::NumericControlKind::Combobox ? baseY + 8
                                                                             : y(control.height));
        if (control.kind == ui::NumericControlKind::Listbox)
            widget->resize(widget->width(), ((y(control.height) - 4) / baseY) * baseY + 4);
        if (control.style & 0x10000u) {
            widget->setFocusPolicy(Qt::StrongFocus);
            if (previous)
                setTabOrder(previous, widget);
            previous = widget;
        }
    }
}
ModelOrderDialog::ModelOrderDialog(bool calculation, const Language& language,
                                   const std::vector<std::pair<std::uint8_t, QString>>& rows,
                                   QWidget* parent)
    : TemplateDialog(calculation ? (language.Code() == "en" ? ui::kNumericCalculationOrderEn
                                                            : ui::kNumericCalculationOrderJa)
                                 : (language.Code() == "en" ? ui::kNumericDrawOrderEn
                                                            : ui::kNumericDrawOrderJa),
                     language, parent) {
    setObjectName(calculation ? "mmd.calculationOrder" : "mmd.drawOrder");
    for (const auto& [slot, label] : rows) {
        auto* item = new QListWidgetItem(label, Rows());
        item->setData(Qt::UserRole, slot);
    }
    Rows()->setCurrentRow(-1);
}
std::vector<std::uint8_t> ModelOrderDialog::Order() const {
    std::vector<std::uint8_t> orderedSlots;
    for (int i = 0; i < Rows()->count(); ++i)
        orderedSlots.push_back(std::uint8_t(Rows()->item(i)->data(Qt::UserRole).toUInt()));
    return orderedSlots;
}
void ModelOrderDialog::ButtonClicked(int id) {
    if (id == 632)
        accept();
    else if (id == 2)
        reject();
    else if (id == 630 || id == 631) {
        const int row = Rows()->currentRow(), next = row + (id == 630 ? -1 : 1);
        if (row < 0 || next < 0 || next >= Rows()->count())
            return;
        auto* item = Rows()->takeItem(row);
        Rows()->insertItem(next, item);
        Rows()->setCurrentRow(next);
    }
    // Original default button 633 is a zero-size Enter sink, not OK (632).
}
void TemplateDialog::Prefill(int id, const QString& text) {
    auto* field = Edit(id);
    const auto bytes = text.toLatin1(); // Generated printf numeric text is ASCII.
    const auto length = ui::NumericPrefillLength(
        std::string_view(bytes.constData(), std::size_t(bytes.size())), english_, field->width());
    field->setText(text.left(qsizetype(length)));
}
bool TemplateDialog::eventFilter(QObject* object, QEvent* event) {
    if (HandleEditComposition(object, event) || HandleEditNavigation(object, event))
        return true;
    if (auto* edit = qobject_cast<QPlainTextEdit*>(object))
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                !(key->modifiers() & (Qt::AltModifier | Qt::MetaModifier))) {
                if (key->modifiers() & Qt::ControlModifier) {
                    if (!edit->isReadOnly())
                        edit->insertPlainText("\n");
                    key->accept();
                    return true;
                }
                if (!edit->property("mmdWantReturn").toBool()) {
                    key->accept();
                    accept();
                    return true;
                }
            }
        }
    return QDialog::eventFilter(object, event);
}
AccessoryOrderDialog::AccessoryOrderDialog(
    const Language& language, const std::vector<std::pair<std::uint8_t, QString>>& rows, int split,
    QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericAccessoryOrderEn
                                             : ui::kNumericAccessoryOrderJa,
                     language, parent) {
    setObjectName("mmd.accessoryOrder");
    for (const auto& [slot, label] : rows) {
        auto* item = new QListWidgetItem(label, Rows());
        item->setData(Qt::UserRole, slot);
    }
    connect(Rows(), &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            Name()->setText(Rows()->item(row)->text());
    });
    connect(Split(), &QLineEdit::textChanged, this, [this] {
        const auto count = Rows()->count();
        const int value = SplitOrder();
        if (value <= 0 || value > count) {
            const QSignalBlocker blocked(Split());
            Split()->setText(QString::number(std::clamp(value, 0, count)));
            Split()->selectAll();
        }
        // LB_SETCURSEL from EN_CHANGE does not send LBN_SELCHANGE: the
        // pending rename edit retains its text while the split selects a row.
        const QSignalBlocker blockedRows(Rows());
        Rows()->setCurrentRow(value >= count ? -1 : std::max(0, value));
    });
    Split()->setText(QString::number(split));
    Rows()->setCurrentRow(-1);
    Name()->clear();
    Name()->installEventFilter(this);
    Split()->installEventFilter(this);
    Name()->setFocus();
}
int AccessoryOrderDialog::SplitOrder() const {
    static const QRegularExpression prefix(QStringLiteral("^\\s*[+-]?[0-9]+"));
    const auto match = prefix.match(Split()->text().left(255));
    if (!match.hasMatch())
        return 0;
    bool valid;
    const auto result = match.captured().trimmed().toLongLong(&valid);
    if (!valid)
        return match.captured().trimmed().startsWith('-') ? INT_MIN : INT_MAX;
    return int(std::clamp(result, qlonglong(INT_MIN), qlonglong(INT_MAX)));
}
std::vector<std::pair<std::uint8_t, std::string>> AccessoryOrderDialog::Order() const {
    std::vector<std::pair<std::uint8_t, std::string>> result;
    for (int i = 0; i < Rows()->count(); ++i) {
        const auto* item = Rows()->item(i);
        result.emplace_back(std::uint8_t(item->data(Qt::UserRole).toUInt()),
                            item->text().toUtf8().toStdString());
    }
    return result;
}
void AccessoryOrderDialog::ButtonClicked(int id) {
    if (id == 632)
        accept();
    else if (id == 2)
        reject();
    else if (id == 630 || id == 631) {
        const int row = Rows()->currentRow(), next = row + (id == 630 ? -1 : 1);
        if (row >= 0 && next >= 0 && next < Rows()->count()) {
            const QSignalBlocker blockedRows(Rows());
            auto* item = Rows()->takeItem(row);
            Rows()->insertItem(next, item);
            Rows()->setCurrentRow(next);
        }
    } else if (id == 633 && !Split()->hasFocus() && !Name()->text().isEmpty()) {
        if (auto* item = Rows()->currentItem())
            item->setText(Name()->text());
    }
}
bool AccessoryOrderDialog::eventFilter(QObject* object, QEvent* event) {
    if (TemplateDialog::eventFilter(object, event))
        return true;
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (object == Name())
                ButtonClicked(633);
            return true;
        }
    }
    return false;
}
NumericDialog::NumericDialog(bool camera, const Language& language,
                             const std::array<float, 7>& values, QWidget* parent)
    : TemplateDialog(camera
                         ? (language.Code() == "en" ? ui::kNumericCameraEn : ui::kNumericCameraJa)
                         : (language.Code() == "en" ? ui::kNumericBoneEn : ui::kNumericBoneJa),
                     language, parent),
      values_(values) {
    setObjectName(camera ? "mmd.numeric.camera" : "mmd.numeric.bone");
    for (unsigned i = 0; i < (camera ? 7u : 6u); ++i) {
        fields_[i] = Edit(i == 6 ? 644 : 637 + i);
        Prefill(i == 6 ? 644 : 637 + i, QString::asprintf("%7.3f", double(values[i])));
    }
    fields_[0]->setFocus(Qt::OtherFocusReason);
    fields_[0]->selectAll();
}
void NumericDialog::accept() {
    auto next = values_;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (!fields_[i])
            continue;
        // The original GetWindowTextA reads a 20-byte buffer including NUL.
        if (!FloatPrefix(fields_[i]->text(), 19, next[i])) {
            fields_[i]->setFocus(Qt::OtherFocusReason);
            fields_[i]->selectAll();
            return;
        }
    }
    values_ = next;
    QDialog::accept();
}
ScreenSizeDialog::ScreenSizeDialog(const Language& language, QSize value, QSize maximum,
                                   QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericScreenEn : ui::kNumericScreenJa,
                     language, parent),
      value_(value), maximum_(maximum) {
    setObjectName("mmd.screenSize");
    Prefill(621, QString::asprintf("%3d", value.width()));
    Prefill(622, QString::asprintf("%3d", value.height()));
    Field(0)->setFocus(Qt::OtherFocusReason);
    Field(0)->selectAll();
}
void ScreenSizeDialog::accept() {
    int values[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        const auto bytes = Field(i)->text().toUtf8().left(19);
        const char* begin = bytes.constData();
        const char* end = begin + bytes.size();
        while (begin < end && (*begin == ' ' || (*begin >= '\t' && *begin <= '\r')))
            ++begin;
        if (begin < end && *begin == '+') {
            ++begin;
            if (begin < end && (*begin == '+' || *begin == '-'))
                begin = end;
        }
        const auto parsed = std::from_chars(begin, end, values[i]);
        const int limit = i ? maximum_.height() : maximum_.width();
        const bool tooLarge = parsed.ec == std::errc::result_out_of_range || values[i] > limit;
        if (parsed.ec != std::errc{} || values[i] <= 0 || tooLarge) {
            if (invalid)
                invalid(tooLarge);
            Field(i)->setFocus(Qt::OtherFocusReason);
            Field(i)->selectAll();
            return;
        }
    }
    value_ = QSize(values[0], values[1]);
    QDialog::accept();
}
CenterBiasDialog::CenterBiasDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericCenterBiasEn : ui::kNumericCenterBiasJa,
                     language, parent) {
    setObjectName("mmd.centerBias");
    for (unsigned axis = 0; axis < 3; ++axis)
        Prefill(601 + int(axis), "0.0");
    Field(0)->setFocus(Qt::OtherFocusReason);
    Field(0)->selectAll();
}
void CenterBiasDialog::accept() {
    auto values = values_;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!FloatPrefix(Field(axis)->text(), 19, values[axis])) {
            Field(axis)->setFocus(Qt::OtherFocusReason);
            Field(axis)->selectAll();
            return;
        }
    values_ = values;
    QDialog::accept();
}
EdgeThicknessDialog::EdgeThicknessDialog(const Language& language, float value, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericEdgeEn : ui::kNumericEdgeJa, language,
                     parent) {
    setObjectName("mmd.edgeThickness");
    Trackbar()->setRange(0, 200);
    Trackbar()->setTickInterval(100);
    Reset(value);
    Field()->setFocus(Qt::OtherFocusReason);
    Field()->selectAll();
    connect(Field(), &QLineEdit::returnPressed, this, [this] {
        float value;
        if (!FloatPrefix(Field()->text(), 7, value)) {
            Field()->selectAll();
            return;
        }
        if (changed)
            changed(value);
        const QSignalBlocker blocked(Trackbar());
        Trackbar()->setValue(EdgePosition(value));
    });
    connect(Trackbar(), &QSlider::valueChanged, this, [this](int position) {
        const float value = float(position) / 100.f;
        if (changed)
            changed(value);
        Prefill(646, QString::asprintf("%3.2f", double(value)));
    });
}
void EdgeThicknessDialog::Reset(float value) {
    const QSignalBlocker blocked(Trackbar());
    Prefill(646, QString::asprintf("%3.2f", double(value)));
    Trackbar()->setValue(EdgePosition(value));
}
GroundShadowDialog::GroundShadowDialog(const Language& language, float value, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericGroundShadowEn
                                             : ui::kNumericGroundShadowJa,
                     language, parent) {
    setObjectName("mmd.groundShadow");
    Trackbar()->setRange(0, 200);
    Trackbar()->setTickInterval(100);
    Reset(value);
    connect(Field(), &QLineEdit::returnPressed, this, [this] {
        float value;
        if (!FloatPrefix(Field()->text(), 7, value)) {
            Field()->selectAll();
            return;
        }
        if (changed)
            changed(value);
        // Original Enter sends TBM_SETPOS to nonexistent control 458. The
        // actual trackbar 625 retains its old position and text is unchanged.
    });
    connect(Trackbar(), &QSlider::valueChanged, this, [this](int position) {
        const float value = float(position) / 100.f;
        if (changed)
            changed(value);
        Prefill(626, QString::asprintf("%3.2f", double(value)));
    });
}
void GroundShadowDialog::Reset(float value) {
    const QSignalBlocker blocked(Trackbar());
    Prefill(626, QString::asprintf("%3.2f", double(value)));
    Trackbar()->setValue(EdgePosition(value));
}
namespace {
QString GravityText(const format::PmmEvaluatedGravity& value, unsigned channel) {
    return channel == 4
               ? QString::number(value.noise)
               : QString::asprintf("%3.2f", double(channel == 0 ? value.acceleration
                                                                : value.direction[channel - 1]));
}
int GravityPosition(float value) {
    return std::clamp(editor::GravityInteger(value * 100.f), -100, 100);
}
}
FrameShiftDialog::FrameShiftDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericFrameShiftEn : ui::kNumericFrameShiftJa,
                     language, parent) {
    setObjectName("mmd.frameShift");
    Prefill(616, "0");
    Field()->setFocus(Qt::OtherFocusReason);
    Field()->selectAll();
}
static std::int32_t Integer20(const QString& value) {
    // Native GetWindowTextA has a 20-byte buffer, then MSVC atoi: decimal
    // prefix, zero for no digits, and saturation at the Windows signed range.
    const auto text = value.toUtf8().left(19);
    const char* begin = text.constData();
    const char* end = begin + text.size();
    while (begin < end && (*begin == ' ' || (*begin >= '\t' && *begin <= '\r')))
        ++begin;
    bool negative = false;
    if (begin < end && (*begin == '+' || *begin == '-'))
        negative = *begin++ == '-';
    std::uint32_t magnitude = 0;
    const std::uint32_t limit = negative ? 2147483648u : 2147483647u;
    for (; begin < end && *begin >= '0' && *begin <= '9'; ++begin) {
        const auto digit = unsigned(*begin - '0');
        if (magnitude > (limit - digit) / 10) {
            magnitude = limit;
            break;
        }
        magnitude = magnitude * 10 + digit;
    }
    return negative ? (magnitude == 2147483648u ? INT_MIN : -std::int32_t(magnitude))
                    : std::int32_t(magnitude);
}
void FrameShiftDialog::accept() {
    value_ = Integer20(Field()->text());
    QDialog::accept();
}
BlinkDialog::BlinkDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericBlinkEn : ui::kNumericBlinkJa, language,
                     parent) {
    setObjectName("mmd.blink");
    Field(0)->setFocus(Qt::OtherFocusReason);
}
void BlinkDialog::accept() {
    for (unsigned i = 0; i < values_.size(); ++i)
        values_[i] = Integer20(Field(i)->text());
    QDialog::accept();
}
MorphScaleDialog::MorphScaleDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericMorphScaleEn : ui::kNumericMorphScaleJa,
                     language, parent) {
    setObjectName("mmd.morphScale");
    Prefill(686, "1.0");
    Prefill(687, "0.0");
    Field(0)->setFocus(Qt::OtherFocusReason);
    Field(0)->selectAll();
}
void MorphScaleDialog::accept() {
    auto next = values_;
    for (unsigned i = 0; i < 2; ++i)
        if (!FloatPrefix(Field(i)->text(), 19, next[i])) {
            Field(i)->setFocus(Qt::OtherFocusReason);
            Field(i)->selectAll();
            return;
        }
    values_ = next;
    QDialog::accept();
}
CameraScaleDialog::CameraScaleDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericCameraScaleEn
                                             : ui::kNumericCameraScaleJa,
                     language, parent) {
    setObjectName("mmd.cameraScale");
    for (unsigned i = 0; i < 16; ++i)
        Prefill(686 + int(i), i % 2 ? "0.0" : "1.0");
    Field(0)->setFocus(Qt::OtherFocusReason);
    Field(0)->selectAll();
}
void CameraScaleDialog::accept() {
    auto next = values_;
    for (unsigned i = 0; i < 16; ++i)
        if (!FloatPrefix(Field(i)->text(), 19, next[i])) {
            Field(i)->setFocus(Qt::OtherFocusReason);
            Field(i)->selectAll();
            return;
        }
    values_ = next;
    QDialog::accept();
}
BoneScaleDialog::BoneScaleDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericBoneScaleEn : ui::kNumericBoneScaleJa,
                     language, parent) {
    setObjectName("mmd.boneScale");
    for (unsigned i = 0; i < 12; ++i)
        Prefill(686 + int(i), i % 2 ? "0.0" : "1.0");
    Field(0)->setFocus(Qt::OtherFocusReason);
    Field(0)->selectAll();
}
void BoneScaleDialog::accept() {
    auto next = values_;
    for (unsigned i = 0; i < 12; ++i)
        if (!FloatPrefix(Field(i)->text(), 19, next[i])) {
            Field(i)->setFocus(Qt::OtherFocusReason);
            Field(i)->selectAll();
            return;
        }
    values_ = next;
    QDialog::accept();
}
PhysicsKeysDialog::PhysicsKeysDialog(const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericPhysicsKeysEn
                                             : ui::kNumericPhysicsKeysJa,
                     language, parent) {
    setObjectName("mmd.physicsKeys");
    Choice()->addItem(language.Text("dialog.numeric.physics_keys.on"));
    Choice()->addItem(language.Text("dialog.numeric.physics_keys.off"));
    Choice()->setCurrentIndex(0);
}
GravityDialog::GravityDialog(const Language& language, const format::PmmEvaluatedGravity& value,
                             QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericGravityEn : ui::kNumericGravityJa,
                     language, parent) {
    setObjectName("mmd.gravity");
    for (unsigned axis = 0; axis < 3; ++axis) {
        Trackbar(axis)->setRange(-100, 100);
        Trackbar(axis)->setTickInterval(1000);
        Trackbar(axis)->setTickPosition(QSlider::TicksBelow);
    }
    Reset(value);
    for (unsigned channel = 0; channel < 5; ++channel) {
        Field(channel)->installEventFilter(this);
        connect(Field(channel), &QLineEdit::textChanged, this, [this, channel] {
            if (!resetting_)
                Apply(channel);
        });
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        connect(Trackbar(axis), &QSlider::valueChanged, this, [this, axis](int position) {
            if (resetting_)
                return;
            const float value = float(double(position) / 100.);
            {
                const QSignalBlocker blocked(Field(axis + 1));
                Field(axis + 1)->setText(QString::asprintf("%3.2f", double(value)));
            }
            if (changed)
                changed(axis + 1, value);
        });
    connect(Noise(), &QCheckBox::toggled, this, [this](bool checked) {
        Field(4)->setEnabled(checked);
        if (!resetting_ && noiseChanged)
            noiseChanged(checked);
    });
}
void GravityDialog::Apply(unsigned channel) {
    float value;
    if (!FloatPrefix(Field(channel)->text(), 255, value))
        return;
    if (channel >= 1 && channel <= 3) {
        const QSignalBlocker blocked(Trackbar(channel - 1));
        Trackbar(channel - 1)->setValue(GravityPosition(value));
    }
    if (changed)
        changed(channel, value);
}
bool GravityDialog::eventFilter(QObject* watched, QEvent* event) {
    if (TemplateDialog::eventFilter(watched, event))
        return true;
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            for (unsigned channel = 0; channel < 5; ++channel)
                if (watched == Field(channel)) {
                    Apply(channel);
                    event->accept();
                    return true; // The native edit subclass consumes Enter before the default button.
                }
    }
    return false;
}
void GravityDialog::Reset(const format::PmmEvaluatedGravity& value) {
    resetting_ = true;
    // These EDITs have ES_AUTOHSCROLL; WM_SETTEXT does not width-truncate.
    for (unsigned channel = 0; channel < 5; ++channel)
        Field(channel)->setText(GravityText(value, channel));
    for (unsigned axis = 0; axis < 3; ++axis)
        Trackbar(axis)->setValue(GravityPosition(value.direction[axis]));
    Noise()->setChecked(value.noiseEnabled != 0);
    Field(4)->setEnabled(value.noiseEnabled != 0);
    resetting_ = false;
}
void GravityDialog::Mirror(const std::function<format::PmmEvaluatedGravity()>& current) {
    resetting_ = true;
    // Native track refresh uses WM_SETTEXT, which sends EN_UPDATE even for
    // hidden EDITs. Each setter can change the next field (zero-vector nudge).
    for (unsigned channel = 0; channel < 5; ++channel) {
        Field(channel)->setText(GravityText(current(), channel));
        Apply(channel);
    }
    const auto value = current();
    for (unsigned axis = 0; axis < 3; ++axis)
        Trackbar(axis)->setValue(GravityPosition(value.direction[axis]));
    Noise()->setChecked(value.noiseEnabled != 0);
    Field(4)->setEnabled(value.noiseEnabled != 0);
    resetting_ = false;
}
void GravityDialog::accept() {
    if (registered)
        registered(); // Modeless register button keeps the dialog open.
}
void GravityDialog::reject() {
    if (closing_)
        return;
    closing_ = true;
    if (closed)
        closed();
    QDialog::reject();
}
void GravityDialog::closeEvent(QCloseEvent* event) {
    // QDialog skips reject()/finished() when a never-shown window is closed.
    // Keep cancellation semantics identical for offscreen and visible hosts.
    reject();
    event->accept();
}
}
