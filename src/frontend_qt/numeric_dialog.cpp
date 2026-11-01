#include "numeric_dialog.hpp"
#include "mikudancestudio/mmd_dialog_edit_metrics.hpp"
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <charconv>
#include <cmath>
namespace mikudancestudio::qt {
namespace {
bool FloatPrefix(const QString& text, int limit, float& result) {
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
TemplateDialog::TemplateDialog(const ui::NumericTemplate& description, const Language& language,
                               QWidget* parent)
    : QDialog(parent), template_(description), english_(language.Code() == "en") {
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
        } else if (control.kind == ui::NumericControlKind::Slider) {
            auto* slider = new QSlider(Qt::Horizontal, this);
            slider->setFocusPolicy(Qt::ClickFocus);
            sliders_[control.id] = slider;
            widget = slider;
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
        if (control.kind == ui::NumericControlKind::Edit ||
            control.kind == ui::NumericControlKind::Button) {
            if (previous)
                setTabOrder(previous, widget);
            previous = widget;
        }
    }
}
void TemplateDialog::Prefill(int id, const QString& text) {
    auto* field = Edit(id);
    const auto bytes = text.toLatin1(); // Generated printf numeric text is ASCII.
    const auto length = ui::NumericPrefillLength(
        std::string_view(bytes.constData(), std::size_t(bytes.size())), english_, field->width());
    field->setText(text.left(qsizetype(length)));
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
}
