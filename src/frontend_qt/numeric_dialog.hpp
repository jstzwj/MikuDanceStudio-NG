#pragma once
#include "language.hpp"
#include "mikudancestudio/mmd_numeric_dialogs.hpp"
#include <QDialog>
#include <QLineEdit>
#include <array>
namespace mikudancestudio::qt {
class NumericDialog final : public QDialog {
public:
    NumericDialog(bool camera, const Language&, const std::array<float, 7>&,
                  QWidget* parent = nullptr);
    const std::array<float, 7>& Values() const { return values_; }
    QLineEdit* Field(unsigned index) const { return fields_.at(index); }
    const ui::NumericTemplate& Template() const { return template_; }
    void accept() override;

private:
    const ui::NumericTemplate& template_;
    std::array<QLineEdit*, 7> fields_{};
    std::array<float, 7> values_{};
};
}
