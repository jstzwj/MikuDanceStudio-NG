#include "numeric_dialog.hpp"
#include "mikudancestudio/mmd_dialog_edit_metrics.hpp"
#include "mikudancestudio/gravity_edit.hpp"
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QWheelEvent>
#include <QStyledItemDelegate>
#include <charconv>
#include <cmath>
namespace mikudancestudio::qt {
namespace {
class OrderRowDelegate final : public QStyledItemDelegate {
public:
    OrderRowDelegate(int height, QObject* parent) : QStyledItemDelegate(parent), height_(height) {}
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
                         ? QStringList{QString::fromUtf8(template_.fontFamily), "Liberation Sans",
                                       "Helvetica", "DejaVu Sans"}
                         : QStringList{QString::fromUtf8(template_.fontFamily), "Noto Sans CJK SC",
                                       "Hiragino Sans", "Microsoft YaHei", "PingFang SC"});
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
            auto* slider = new DialogTrackbar(this);
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
        if (control.kind == ui::NumericControlKind::Edit ||
            control.kind == ui::NumericControlKind::Button) {
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
    Trackbar()->setTickPosition(QSlider::TicksBelow);
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
    return TemplateDialog::eventFilter(watched, event);
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
