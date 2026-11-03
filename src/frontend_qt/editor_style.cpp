#include "editor_style.hpp"
#include "editor_font.hpp"
#include <QApplication>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
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
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
#ifdef _WIN32
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
