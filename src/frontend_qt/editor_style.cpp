#include "editor_style.hpp"
#include "editor_font.hpp"
#include "mikudancestudio/mmd_trackbar_geometry.hpp"
#include <QApplication>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
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
            DrawWindowsSunkenBorder(*painter, option->rect);
            return;
        }
        // Original EDIT controls use WS_BORDER, not WS_EX_CLIENTEDGE. Qt's
        // themed frame changes color on focus/disable; the native plain border
        // consistently uses COLOR_WINDOWFRAME in these states.
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
