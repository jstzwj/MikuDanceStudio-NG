#include "numeric_dialog.hpp"
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <charconv>
#include <cmath>
namespace mikudancestudio::qt {
TemplateDialog::TemplateDialog(const ui::NumericTemplate& description, const Language& language,
                               QWidget* parent)
    : QDialog(parent), template_(description) {
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
            auto* field = new QLineEdit(this);
            edits_[control.id] = field;
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
        fields_[i]->setText(QString::asprintf("%7.3f", double(values[i])));
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
ScreenSizeDialog::ScreenSizeDialog(const Language& language, QSize value, QSize maximum,
                                   QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericScreenEn : ui::kNumericScreenJa,
                     language, parent),
      value_(value), maximum_(maximum) {
    setObjectName("mmd.screenSize");
    Field(0)->setText(QString::asprintf("%3d", value.width()));
    Field(1)->setText(QString::asprintf("%3d", value.height()));
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
}
