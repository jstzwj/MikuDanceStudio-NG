#include "english_name_dialog.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include <QCloseEvent>
#include <QKeyEvent>

namespace mikudancestudio::qt {
namespace {
constexpr int kCombos[]{669, 673, 677}, kEdits[]{667, 672, 676, 680};
QString Display(const std::string& bytes) {
    return QString::fromUtf8(editor::EnglishNameDisplay(bytes).c_str());
}
}
EnglishNameDialog::EnglishNameDialog(editor::EditorSession& session, const Language& language,
                                     QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericEnglishNameEn
                                             : ui::kNumericEnglishNameJa,
                     language, parent),
      session_(session), slot_(session.Selected().model),
      resource_(session.Runtime().Resource(slot_)) {
    setObjectName("mmd.englishNames");
    format::PmmModelEnglishNames names;
    std::string reason;
    if (!session_.ModelEnglishNames(slot_, names, reason))
        return;
    Edit(667)->setText(Display(names.name));
    // Native initialization expands LF and consumes the byte following CR.
    std::string comment;
    for (std::size_t i = 0; i < names.comment.size();) {
        const char c = names.comment[i++];
        comment += c == '\r' ? '\n' : c;
        if (c == '\r' && i < names.comment.size())
            ++i;
    }
    Multiline(668)->setPlainText(Display(comment));
    for (const auto& group : resource_->DisplayGroups())
        hasFacial_ |= group.role == 2;
    firstGroup_ = hasFacial_ ? 3 : 2;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& entries = i == 0 ? names.bones : i == 1 ? names.morphs : names.groups;
        const unsigned first = i == 0 ? 0 : i == 1 ? 1 : firstGroup_;
        for (std::size_t n = first; n < entries.size(); ++n)
            Combo(kCombos[i])->addItem(Display(entries[n]));
        Combo(kCombos[i])->setCurrentIndex(Combo(kCombos[i])->count() ? 0 : -1);
        if (first < entries.size() && (i != 1 || hasFacial_))
            Edit(kEdits[i + 1])->setText(Display(entries[first]));
        connect(Combo(kCombos[i]), &QComboBox::currentIndexChanged, this,
                [this, i](int index) { Select(i, index); });
    }
    for (int edit : kEdits)
        Edit(edit)->installEventFilter(this);
    session_.SetEditMode(0);
    if (!names.bones.empty())
        SelectBone(0); // Native initialization selects bone zero even for hidden types.
}
bool EnglishNameDialog::Valid() const {
    return resource_ && session_.Selected().model == slot_ &&
           session_.Runtime().Resource(slot_) == resource_;
}
void EnglishNameDialog::Invalidate() {
    closing_ = true;
    done(QDialog::Rejected);
}
void EnglishNameDialog::SelectBone(unsigned index) {
    int channel;
    std::string reason;
    if (session_.Runtime().ResourceBoneIndex(slot_, int(index), channel, reason))
        session_.SelectBones({unsigned(channel)}, channel, reason);
}
bool EnglishNameDialog::Commit(unsigned kind) {
    if (!Valid())
        return false;
    if (kind && Combo(kCombos[kind - 1])->count() == 0)
        return true;
    if (kind == 2 && !hasFacial_)
        return true;
    const auto target = kind == 0   ? editor::EnglishNameKind::Model
                        : kind == 1 ? editor::EnglishNameKind::Bone
                        : kind == 2 ? editor::EnglishNameKind::Morph
                                    : editor::EnglishNameKind::Group;
    const unsigned index = kind == 0 ? 0
                                     : unsigned(cursors_[kind - 1]) + (kind == 2   ? 1u
                                                                       : kind == 3 ? firstGroup_
                                                                                   : 0u);
    std::string reason;
    if (!session_.SetEnglishName(slot_, target, index,
                                 Edit(kEdits[kind])->text().toUtf8().toStdString(), reason)) {
        if (failed)
            failed(reason);
        return false;
    }
    if (changed)
        changed();
    return true;
}
void EnglishNameDialog::Select(unsigned kind, int index) {
    if (closing_ || !Valid() || index < 0 || (kind == 1 && !hasFacial_))
        return;
    if (!Commit(kind + 1))
        return;
    cursors_[kind] = index;
    if (kind == 0) {
        const auto type = resource_->EditingBones()[unsigned(index)].type;
        std::string reason;
        if (type == cpu_bone::BoneType::Effector || type == cpu_bone::BoneType::InertTip ||
            type == cpu_bone::BoneType::CoRotate)
            session_.SelectBones({}, -1, reason);
        else
            SelectBone(unsigned(index));
    }
    format::PmmModelEnglishNames names;
    std::string reason;
    if (!session_.ModelEnglishNames(slot_, names, reason))
        return;
    const auto& entries = kind == 0 ? names.bones : kind == 1 ? names.morphs : names.groups;
    const unsigned entry = unsigned(index) + (kind == 1 ? 1u : kind == 2 ? firstGroup_ : 0u);
    if (entry < entries.size()) {
        auto* edit = Edit(kEdits[kind + 1]);
        edit->setText(Display(entries[entry]));
        edit->selectAll();
        edit->setFocus();
    }
    if (changed)
        changed();
}
bool EnglishNameDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            for (unsigned i = 0; i < 4; ++i)
                if (watched == Edit(kEdits[i])) {
                    Commit(i);
                    return true;
                }
    }
    return TemplateDialog::eventFilter(watched, event);
}
void EnglishNameDialog::ButtonClicked(int id) {
    if (id == 1 || id == 2)
        Finish(id == 1);
    else
        for (unsigned i = 0; i < 3; ++i) {
            const int next = 670 + int(i) * 4;
            if (id != next && id != next + 1)
                continue;
            auto* combo = Combo(kCombos[i]);
            const auto count = combo->count();
            if (!count)
                return;
            const int index = (cursors_[i] + (id == next ? 1 : count - 1)) % count;
            if (combo->currentIndex() == index)
                Select(i, index); // A one-item native combo still commits the edit.
            else
                combo->setCurrentIndex(index);
            return;
        }
}
void EnglishNameDialog::Finish(bool accepted) {
    if (closing_)
        return;
    if (!Valid()) {
        Invalidate();
        return;
    }
    auto comment = Multiline(668)->toPlainText();
    comment.replace('\n', "\r\n"); // ANSI multiline edit truncates before CR removal.
    std::string reason;
    if (!session_.SetEnglishName(slot_, editor::EnglishNameKind::Comment, 0,
                                 comment.toUtf8().toStdString(), reason)) {
        if (failed)
            failed(reason);
        return;
    }
    for (unsigned i = 0; i < 4; ++i)
        if (!Commit(i))
            return;
    if (accepted && !session_.MarkEnhancedModel(slot_, reason))
        return;
    closing_ = true;
    if (changed)
        changed();
    done(accepted ? QDialog::Accepted : QDialog::Rejected);
    if (accepted && preserve)
        preserve();
}
void EnglishNameDialog::accept() {
    Finish(true);
}
void EnglishNameDialog::reject() {
    Finish(false);
}
void EnglishNameDialog::closeEvent(QCloseEvent* event) {
    Finish(false);
    event->accept();
}
}
