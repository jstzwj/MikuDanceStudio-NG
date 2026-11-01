#include "numeric_dialog.hpp"
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <cmath>
namespace mikudancestudio::qt {
NumericDialog::NumericDialog(bool camera, const Language& language,
                             const std::array<float, 7>& values, QWidget* parent)
    : QDialog(parent),
      template_(camera ? (language.Code() == "en" ? ui::kNumericCameraEn : ui::kNumericCameraJa)
                       : (language.Code() == "en" ? ui::kNumericBoneEn : ui::kNumericBoneJa)),
      values_(values) {
    setObjectName(camera ? "mmd.numeric.camera" : "mmd.numeric.bone");
    setWindowTitle(language.Text(template_.title));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QFont font;
    font.setFamilies(language.Code() == "en"
                         ? QStringList{"Arial", "Liberation Sans", "Helvetica", "DejaVu Sans"}
                         : QStringList{"MS PGothic", "Noto Sans CJK SC", "Hiragino Sans",
                                       "Microsoft YaHei", "PingFang SC"});
    font.setPixelSize(template_.pointSize * 96 / 72);
    setFont(font);
    // Original 96-DPI Arial / MS PGothic dialog base units, independently
    // measured in a memory DC. Qt logical pixels then scale with the screen.
    const int baseX = 7, baseY = language.Code() == "en" ? 15 : 12;
    const auto x = [&](int v) { return (v * baseX + 2) / 4; };
    const auto y = [&](int v) { return (v * baseY + 4) / 8; };
    setFixedSize(x(template_.width), y(template_.height));
    QWidget* previous = nullptr;
    for (std::size_t i = 0; i < template_.count; ++i) {
        const auto& control = template_.controls[i];
        QWidget* widget = nullptr;
        if (control.kind == ui::NumericControlKind::Edit) {
            const auto index = control.id == 644 ? 6 : control.id - 637;
            auto* field = new QLineEdit(this);
            field->setText(QString::asprintf("%7.3f", double(values[index])));
            fields_[index] = field;
            widget = field;
        } else if (control.kind == ui::NumericControlKind::Static) {
            auto* label = new QLabel(language.Text(control.message), this);
            label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            widget = label;
        } else {
            auto* button = new QPushButton(language.Text(control.message), this);
            button->setAutoDefault(false);
            button->setDefault(control.id == 1);
            connect(button, &QPushButton::clicked, this,
                    [this, id = control.id] { id == 1 ? accept() : reject(); });
            widget = button;
        }
        widget->setObjectName(QString("numeric.control.%1").arg(i));
        widget->setProperty("mmdControlId", control.id);
        widget->setGeometry(x(control.x), y(control.y), x(control.width), y(control.height));
        if (control.kind != ui::NumericControlKind::Static) {
            if (previous)
                setTabOrder(previous, widget);
            previous = widget;
        }
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
        const auto bytes = fields_[i]->text().toUtf8().left(19);
        const char* begin = bytes.constData();
        const char* end = begin + bytes.size();
        while (begin < end && (*begin == ' ' || (*begin >= '\t' && *begin <= '\r')))
            ++begin;
        // libc++ on our macOS baseline has no floating-point from_chars.
        // Recognize the same decimal prefix, then convert in the C locale;
        // the desktop's decimal separator must not change PMM input values.
        static const QRegularExpression prefix(
            QStringLiteral("^[+-]?(?:(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)"
                           "(?:[eE][+-]?[0-9]+)?|inf(?:inity)?|nan)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = prefix.match(QString::fromLatin1(begin, end - begin));
        double parsed = 0;
        bool valid = true;
        if (match.hasMatch())
            parsed = QLocale::c().toDouble(match.captured(), &valid);
        // Native atof accepts a decimal prefix and maps nonnumeric text to 0.
        // Reject non-finite/overflow values at the shared editor's boundary.
        if (!valid || !std::isfinite(parsed) || !std::isfinite(float(parsed))) {
            fields_[i]->setFocus(Qt::OtherFocusReason);
            fields_[i]->selectAll();
            return;
        }
        next[i] = float(parsed);
    }
    values_ = next;
    QDialog::accept();
}
}
