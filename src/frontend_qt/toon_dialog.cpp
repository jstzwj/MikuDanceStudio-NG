#include "toon_dialog.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/english_name_edit.hpp"
namespace mikudancestudio::qt {
ToonDialog::ToonDialog(editor::EditorSession& session, const Language& language, QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericToonEn : ui::kNumericToonJa, language,
                     parent),
      session_(session), slot_(session.Selected().model),
      resource_(session.Runtime().Resource(slot_)) {
    setObjectName("mmd.toonTextures");
    std::array<std::string, 10> names;
    std::string reason;
    if (session_.ModelToonNames(slot_, names, reason))
        for (unsigned i = 0; i < 10; ++i)
            Field(i)->setText(QString::fromUtf8(text::DecodeCp932Display(names[i]).c_str()));
}
void ToonDialog::accept() {
    if (slot_ != session_.Selected().model || session_.Runtime().Resource(slot_) != resource_) {
        reject();
        return;
    }
    std::array<std::string, 10> names;
    for (unsigned i = 0; i < 10; ++i)
        names[i] = Field(i)->text().toUtf8().toStdString();
    std::vector<unsigned> absent;
    std::string reason;
    const bool ok = session_.SetModelToonNames(slot_, names, absent, reason);
    for (unsigned i : absent)
        if (missing)
            missing(QString::fromUtf8(
                text::DecodeCp932Display(editor::EnglishNameBytes(names[i], 255)).c_str()));
    done(ok ? QDialog::Accepted : QDialog::Rejected);
}
}
