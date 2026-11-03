#include "editor_style.hpp"
#include "editor_font.hpp"
#include "mikudancestudio/mmd_trackbar_geometry.hpp"
#include <QApplication>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
#include <QStyleOptionComboBox>
#include <QImage>
#include <QPainter>
#include <QVariant>
#include <QWidget>
#ifdef _WIN32
#include "editor_style_windows.hpp"
#endif

namespace mikudancestudio::qt {
namespace {
class EditorStyle final : public QProxyStyle {
public:
    explicit EditorStyle(QStyle* base) : QProxyStyle(base) {
        setObjectName(QStringLiteral("mmd.editorStyle"));
    }
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl part,
                         const QWidget* widget) const override {
#ifdef _WIN32
        if (control == CC_ComboBox && part == SC_ComboBoxEditField && widget &&
            widget->property("mmdControlId").isValid() &&
            baseStyle()->objectName() == "windowsvista")
            if (const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option))
                if (!combo->editable && combo->direction == Qt::LeftToRight)
                    return QProxyStyle::subControlRect(control, option, part, widget)
                        .adjusted(0, 0, -1, 0);
#endif
        if (control == CC_Slider && widget && widget->property("mmdNativeTrackbar").toBool())
            if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option)) {
                const auto geometry = SliderGeometry(*slider);
                const auto rectangle = part == SC_SliderHandle ? geometry.thumb : geometry.channel;
                if (part == SC_SliderHandle || part == SC_SliderGroove)
                    return QRect(rectangle.x, rectangle.y, rectangle.width, rectangle.height)
                        .translated(slider->rect.topLeft());
            }
        return QProxyStyle::subControlRect(control, option, part, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override {
        if (metric == PM_DefaultFrameWidth && widget &&
            widget->property("mmdNativeSunkenEdit").toBool())
            return 2;
        if (widget && widget->property("mmdNativeTrackbar").toBool())
            if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option)) {
                const auto geometry = SliderGeometry(*slider);
                if (metric == PM_SliderLength)
                    return geometry.thumb.width;
                if (metric == PM_SliderSpaceAvailable)
                    return std::max(0, geometry.channel.width - geometry.thumb.width);
            }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override {
#ifdef _WIN32
        if (control == CC_Slider && widget && widget->property("mmdNativeTrackbar").toBool() &&
            baseStyle()->objectName() == "windowsvista")
            if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option))
                if (DrawWindowsTrackbar(*painter, *slider, SliderGeometry(*slider)))
                    return;
#endif
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
#ifdef _WIN32
        if (element == PE_Frame && widget && widget->property("mmdNativeSunkenBorder").toBool() &&
            baseStyle()->objectName() == "windowsvista") {
            // USER32 WM_PRINT on a never-shown list uses its classic edge.
            // Real displayed LISTBOX windows use the active ListBox theme.
            if (widget->isVisible() && DrawWindowsListBorder(*painter, option->rect, option->state))
                return;
            DrawWindowsSunkenBorder(*painter, option->rect);
            return;
        }
        // DS_3DLOOK turns the resource EDIT's WS_BORDER into CLIENTEDGE.
        // USER32 retains the white inner CLIENTEDGE even when disabled. Qt's
        // default edit frame instead fills it with the disabled background.
        if (element == PE_FrameLineEdit && widget &&
            widget->property("mmdNativeSunkenEdit").toBool() &&
            baseStyle()->objectName() == "windowsvista") {
            if (widget->isVisible() && DrawWindowsEditBorder(*painter, option->rect, option->state))
                return;
            if (widget->isVisible()) {
                QProxyStyle::drawPrimitive(element, option, painter, widget);
                return;
            }
            DrawWindowsSunkenBorder(*painter, option->rect);
            return;
        }
        // Main-window edits are created directly, without DS_3DLOOK conversion.
        if (element == PE_FrameLineEdit && widget &&
            widget->property("mmdNativeWindowBorder").toBool() &&
            baseStyle()->objectName() == "windowsvista") {
            DrawWindowsWindowBorder(*painter, option->rect);
            return;
        }
        if ((element == PE_IndicatorRadioButton || element == PE_IndicatorCheckBox) && widget &&
            baseStyle()->objectName() == "windowsvista") {
            // Qt's theme cache keeps the full-size image even when given a
            // shorter rectangle. Win32 stretches the native theme to that rect.
            const auto rectangle = option->rect.intersected(widget->rect());
            if (rectangle.height() < option->rect.height() &&
                DrawWindowsButtonIndicator(*painter, rectangle, option->palette.window().color(),
                                           option->state, element == PE_IndicatorRadioButton))
                return;
        }
#endif
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override {
#ifdef _WIN32
        if (element == CE_ComboBoxLabel && widget && widget->property("mmdControlId").isValid() &&
            baseStyle()->objectName() == "windowsvista")
            if (const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
                if (!combo->editable && combo->currentIcon.isNull() &&
                    combo->direction == Qt::LeftToRight) {
                    const auto ratio = painter->device()->devicePixelRatioF();
                    QImage background(qRound(combo->rect.width() * ratio),
                                      qRound(combo->rect.height() * ratio), QImage::Format_RGB32);
                    background.setDevicePixelRatio(ratio);
                    background.fill(combo->palette.window().color());
                    auto blank = *combo;
                    blank.rect.moveTopLeft({0, 0});
                    blank.currentText.clear();
                    QPainter backdrop(&background);
                    QProxyStyle::drawComplexControl(CC_ComboBox, &blank, &backdrop, widget);
                    backdrop.end();
                    auto field =
                        subControlRect(CC_ComboBox, &blank, SC_ComboBoxEditField, widget);
                    if (DrawWindowsComboText(background, field, widget->font(), combo->currentText,
                                             combo->state & State_Enabled,
                                             combo->state & State_HasFocus)) {
                        painter->drawImage(combo->rect.topLeft(), background);
                        return;
                    }
                }
            }
        if ((element == CE_PushButton || element == CE_CheckBox || element == CE_RadioButton) &&
            widget && widget->property("mmdControlId").isValid() &&
            baseStyle()->objectName() == "windowsvista")
            if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option)) {
                if (!button->icon.isNull() ||
                    (button->features & (QStyleOptionButton::Flat | QStyleOptionButton::HasMenu |
                                         QStyleOptionButton::CommandLinkButton)) ||
                    button->direction != Qt::LeftToRight)
                    return QProxyStyle::drawControl(element, option, painter, widget);
                const auto indicator =
                    subElementRect(element == CE_RadioButton ? SE_RadioButtonIndicator
                                                             : SE_CheckBoxIndicator,
                                   option, widget)
                        .intersected(widget->rect());
                if (DrawWindowsButton(*painter, *button, widget->font(),
                                      element == CE_PushButton ? nullptr : &indicator,
                                      element == CE_RadioButton,
                                      styleHint(SH_UnderlineShortcut, option, widget)))
                    return;
            }
#endif
        // Win32 keeps the default-button outline on an inactive dialog too.
        // Qt's Vista style otherwise drops it. Do not activate the real window
        // or start a default-button pulse for an inactive dialog.
        if (element == CE_PushButtonBevel && baseStyle()->objectName() == "windowsvista")
            if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option))
                if ((button->features & QStyleOptionButton::DefaultButton) &&
                    !(button->state & State_Active)) {
                    auto native = *button;
                    native.state |= State_Active;
                    native.styleObject = nullptr;
                    QProxyStyle::drawControl(element, &native, painter, widget);
                    return;
                }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

private:
    static ui::TrackbarGeometry SliderGeometry(const QStyleOptionSlider& slider) {
        const int position =
            slider.upsideDown
                ? int(std::int64_t(slider.maximum) + slider.minimum - slider.sliderPosition)
                : slider.sliderPosition;
        return ui::DialogTrackbarGeometry(slider.rect.width(), slider.rect.height(),
                                          slider.tickPosition != QSlider::NoTicks, slider.minimum,
                                          slider.maximum, position);
    }
};
}
void InstallEditorStyle(QApplication& app) {
    InstallEditorFonts();
    if (app.style()->objectName() == "mmd.editorStyle")
        return;
    QStyle* base = nullptr;
#ifdef _WIN32
    if (QGuiApplication::platformName() == "windows")
        base = QStyleFactory::create(QStringLiteral("windowsvista"));
#endif
    if (!base)
        base = QStyleFactory::create(QStringLiteral("Fusion"));
    // Retain the platform palette. standardPalette() on QWindowsStyle is the
    // classic beige palette, not the original's current common-controls theme.
    app.setStyle(new EditorStyle(base));
}
}
