#pragma once
#include "language.hpp"
#include "mikudancestudio/mmd_numeric_dialogs.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>
#include "mikudancestudio/pmm_scene_evaluation.hpp"
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
    QCheckBox* Checkbox(int id) const { return checkboxes_.at(id); }
    void Prefill(int id, const QString&);

private:
    const ui::NumericTemplate& template_;
    bool english_;
    std::map<int, QLineEdit*> edits_;
    std::map<int, QSlider*> sliders_;
    std::map<int, QCheckBox*> checkboxes_;
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
    void Reset(float value);
    // Modeless: Enter applies the field; closing discards uncommitted text.
    void accept() override {}
    std::function<void(float)> changed;
};
class MorphScaleDialog final : public TemplateDialog {
public:
    MorphScaleDialog(const Language&, QWidget* parent = nullptr);
    QLineEdit* Field(unsigned index) const { return Edit(686 + int(index)); }
    const std::array<float, 2>& Values() const { return values_; }
    void accept() override;

private:
    std::array<float, 2> values_{1.f, 0.f};
};
class CameraScaleDialog final : public TemplateDialog {
public:
    CameraScaleDialog(const Language&, QWidget* parent = nullptr);
    QLineEdit* Field(unsigned index) const { return Edit(686 + int(index)); }
    const std::array<float, 16>& Values() const { return values_; }
    void accept() override;

private:
    std::array<float, 16> values_{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
};
class BoneScaleDialog final : public TemplateDialog {
public:
    BoneScaleDialog(const Language&, QWidget* parent = nullptr);
    QLineEdit* Field(unsigned index) const { return Edit(686 + int(index)); }
    const std::array<float, 12>& Values() const { return values_; }
    void accept() override;

private:
    std::array<float, 12> values_{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
};
class GravityDialog final : public TemplateDialog {
public:
    GravityDialog(const Language&, const format::PmmEvaluatedGravity&, QWidget* parent = nullptr);
    QLineEdit* Field(unsigned channel) const { return Edit(709 + int(channel)); }
    QSlider* Trackbar(unsigned axis) const { return Slider(637 + int(axis)); }
    QCheckBox* Noise() const { return Checkbox(731); }
    void Reset(const format::PmmEvaluatedGravity&);
    void Mirror(const std::function<format::PmmEvaluatedGravity()>&);
    void accept() override;
    void reject() override;
    std::function<void(unsigned, float)> changed;
    std::function<void(bool)> noiseChanged;
    std::function<void()> registered;
    std::function<void()> closed;

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    void Apply(unsigned);
    bool resetting_ = false;
    bool closing_ = false;
};
}
