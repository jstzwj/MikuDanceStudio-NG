#include "preferences_dialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
namespace mikudancestudio::qt {
PreferencesDialog::PreferencesDialog(Language& language, std::function<bool(const QString&)> apply,
                                     QWidget* parent)
    : QDialog(parent), language_(language), apply_(std::move(apply)),
      languages_(new QComboBox(this)), label_(new QLabel(this)), description_(new QLabel(this)),
      buttons_(new QDialogButtonBox(
          QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this)) {
    setObjectName("preferences");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowModality(Qt::WindowModal);
    languages_->setObjectName("preferences.language");
    languages_->addItem(QString(), "system");
    for (const auto& info : Language::Available())
        languages_->addItem(info.nativeName, info.code);
    label_->setBuddy(languages_);
    description_->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(label_, languages_);
    layout->addLayout(form);
    layout->addWidget(description_);
    layout->addWidget(buttons_);
    connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this] { Apply(); });
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        if (Apply())
            accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    Retranslate();
    Reset();
}
void PreferencesDialog::Reset() {
    languages_->setCurrentIndex(languages_->findData(language_.Selection()));
}
bool PreferencesDialog::Apply() {
    if (!apply_(languages_->currentData().toString()))
        return false;
    Retranslate();
    return true;
}
void PreferencesDialog::Retranslate() {
    setWindowTitle(language_.Text("preferences.title"));
    label_->setText(language_.Text("preferences.language"));
    description_->setText(language_.Text("preferences.language.help"));
    languages_->setItemText(0, language_.Text("preferences.system"));
    buttons_->button(QDialogButtonBox::Ok)->setText(language_.Text("preferences.ok"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(language_.Text("preferences.cancel"));
    buttons_->button(QDialogButtonBox::Apply)->setText(language_.Text("preferences.apply"));
    setMinimumWidth(std::max(360, minimumSizeHint().width()));
    adjustSize();
}
}
