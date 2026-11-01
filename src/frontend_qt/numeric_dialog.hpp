#pragma once
#include "language.hpp"
#include "mikudancestudio/mmd_numeric_dialogs.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QSlider>
#include <array>
#include <functional>
#include <map>
namespace mikudancestudio::qt {
class TemplateDialog : public QDialog {
public:
    const ui::NumericTemplate& Template() const { return template_; }

protected:
    TemplateDialog(const ui::NumericTemplate&, const Language&, QWidget*);
    QLineEdit* Edit(int id) const { return edits_.at(id); }
    QSlider* Slider(int id) const { return sliders_.at(id); }
    void Prefill(int id, const QString&);

private:
    const ui::NumericTemplate& template_;
    bool english_;
    std::map<int, QLineEdit*> edits_;
    std::map<int, QSlider*> sliders_;
};
class NumericDialog final : public TemplateDialog {
public:
    NumericDialog(bool camera, const Language&, const std::array<float, 7>&,
                  QWidget* parent = nullptr);
    const std::array<float, 7>& Values() const { return values_; }
    QLineEdit* Field(unsigned index) const { return fields_.at(index); }
    void accept() override;

private:
    std::array<QLineEdit*, 7> fields_{};
    std::array<float, 7> values_{};
};
class ScreenSizeDialog final : public TemplateDialog {
public:
    ScreenSizeDialog(const Language&, QSize value, QSize maximum, QWidget* parent = nullptr);
    QLineEdit* Field(unsigned index) const { return Edit(index ? 622 : 621); }
    QSize Value() const { return value_; }
    void accept() override;
    // Host supplies localized reporting; failed validation leaves this dialog open.
    std::function<void(bool tooLarge)> invalid;

private:
    QSize value_, maximum_;
};
class EdgeThicknessDialog final : public TemplateDialog {
public:
    EdgeThicknessDialog(const Language&, float value, QWidget* parent = nullptr);
    QLineEdit* Field() const { return Edit(646); }
    QSlider* Trackbar() const { return Slider(647); }
    // Modeless: Enter applies the field; closing discards uncommitted text.
    void accept() override {}
    std::function<void(float)> changed;
};
}
