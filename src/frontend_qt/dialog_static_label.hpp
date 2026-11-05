#pragma once
#include <QLabel>
#include <cstdint>

namespace mikudancestudio::qt {
// Original STATIC wrapping in fixed dialog geometry. Portable font fitting
// changes glyph width only; it never enlarges controls or truncates text.
class DialogStaticLabel final : public QLabel {
public:
    DialogStaticLabel(const QString& text, std::uint32_t style, QWidget* parent);

protected:
    bool event(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    void FitPortableFont();
    std::uint32_t nativeStyle_;
    QString fittedText_;
    bool fitting_ = false;
};
}
