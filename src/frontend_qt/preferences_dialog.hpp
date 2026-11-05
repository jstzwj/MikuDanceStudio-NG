#pragma once
#include "language.hpp"
#include <QDialog>
#include <functional>
class QComboBox;
class QDialogButtonBox;
class QLabel;
namespace mikudancestudio::qt {
class PreferencesDialog final : public QDialog {
public:
    PreferencesDialog(Language&, std::function<bool(const QString&)>, QWidget* parent);
    void Retranslate();
    void Reset();

private:
    Language& language_;
    std::function<bool(const QString&)> apply_;
    QComboBox* languages_;
    QLabel *label_, *description_;
    QDialogButtonBox* buttons_;
    bool Apply();
};
}
