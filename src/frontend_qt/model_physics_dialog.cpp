#include "model_physics_dialog.hpp"
#include "model_labels.hpp"
#include <QButtonGroup>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <cstring>

namespace mikudancestudio::qt {
namespace {
constexpr int kLimits[]{757, 759, 761, 747, 758, 760, 763, 765, 767, 762, 764, 766};
QString Number(const char* format, float value) {
    char text[256];
    std::snprintf(text, sizeof text, format, double(value));
    return QString::fromLatin1(text);
}
QString Name(const char* bytes) {
    return QString::fromUtf8(editor::EnglishNameDisplay({bytes, strnlen(bytes, 20)}).c_str());
}
}
ModelPhysicsDialog::ModelPhysicsDialog(editor::EditorSession& session, const Language& language,
                                       QWidget* parent)
    : TemplateDialog(language.Code() == "en" ? ui::kNumericModelPhysicsEn
                                             : ui::kNumericModelPhysicsJa,
                     language, parent),
      session_(session), language_(language), slot_(session.Selected().model),
      resource_(session.Runtime().Resource(slot_)) {
    setObjectName("mmd.modelPhysics");
    std::string reason;
    if (!session_.BeginModelPhysicsEdit(slot_, edit_, reason)) {
        closing_ = true;
        return;
    }
    for (const auto& ids : {std::vector<int>{726, 727, 728}, {729, 730}, {800, 801}}) {
        auto* group = new QButtonGroup(this);
        for (int id : ids)
            group->addButton(Radio(id), id);
    }
    Combo(707)->addItem(language_.Text("dialog.physics.none"));
    for (unsigned i = 0; i < resource_->Document().bones.size(); ++i) {
        const auto name = ModelTrackLabel(session_, slot_, i, false, language.Code() == "en");
        Combo(707)->addItem(name);
        Combo(743)->addItem(name);
    }
    Combo(743)->setCurrentIndex(-1);
    for (int i = 1; i <= 15; ++i)
        Combo(708)->addItem(QString::number(i));
    Combo(708)->addItem(language_.Text("dialog.physics.floor_group"));
    Lists();
    body_ = edit_.bodyOrder.empty() ? -1 : 0;
    joint_ = edit_.jointOrder.empty() ? -1 : 0;
    Page(true);
    FillBody();
    FillJoint();
    for (int id : {704, 736, 707, 708, 741, 742, 743})
        connect(Combo(id), &QComboBox::activated, this,
                [this, id](int index) { Select(id, index); });
    for (auto* field : findChildren<QLineEdit*>())
        field->installEventFilter(this);
    connect(Checkbox(731), &QCheckBox::clicked, this, [this] { ButtonClicked(731); });
}
QWidget* ModelPhysicsDialog::Control(int id) const {
    for (auto* widget : findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly))
        if (widget->property("mmdControlId").toInt() == id)
            return widget;
    return nullptr;
}
bool ModelPhysicsDialog::Valid() const {
    return resource_ && session_.Selected().model == slot_ &&
           session_.Runtime().Resource(slot_) == resource_;
}
bool ModelPhysicsDialog::Body() const {
    return body_ >= 0 && std::size_t(body_) < edit_.bodies.size();
}
bool ModelPhysicsDialog::Joint() const {
    return joint_ >= 0 && std::size_t(joint_) < edit_.joints.size();
}
bool ModelPhysicsDialog::Drag(unsigned mode, int dy, bool shift) {
    if (!Active())
        return false;
    const bool jointPage = JointPage();
    const auto echo = edit_.Drag(jointPage, jointPage ? joint_ : body_, mode, dy, shift);
    if (echo.control)
        Edit(echo.control)->setText(Number(echo.format, echo.value));
    return true;
}
void ModelPhysicsDialog::Invalidate() {
    if (closing_)
        return;
    session_.AbandonModelPhysicsEdit(slot_);
    closing_ = true;
    done(QDialog::Rejected);
}
void ModelPhysicsDialog::Lists() {
    for (int id : {704, 736, 741, 742}) {
        QSignalBlocker block(Combo(id));
        Combo(id)->clear();
        for (int index : id == 736 ? edit_.jointOrder : edit_.bodyOrder)
            Combo(id)->addItem(
                Name(id == 736 ? edit_.joints[index].pmdName : edit_.bodies[index].pmdName), index);
        Combo(id)->setCurrentIndex(Combo(id)->count() ? 0 : -1);
    }
}
void ModelPhysicsDialog::Page(bool body) {
    Radio(body ? 800 : 801)->setChecked(true);
    for (const auto& id : {685, 735})
        for (int i = id; i <= (id == 685 ? 734 : 799); ++i)
            if (auto* control = Control(i))
                control->setVisible((id == 685) == body);
    // Native page flip shows the entire range, including hidden shape fields.
}
void ModelPhysicsDialog::Shape(int shape) {
    Radio(726 + shape)->setChecked(true);
    static_cast<QLabel*>(Control(732))
        ->setText(language_.Text(shape == 1 ? "dialog.physics.width" : "dialog.physics.radius"));
    for (int id : {710, 733, 711, 734})
        Control(id)->setVisible(Radio(800)->isChecked() &&
                                (id == 710 || id == 733 ? shape != 0 : shape == 1));
    if (shape) {
        static_cast<QLabel*>(Control(733))->setText(language_.Text("dialog.physics.height"));
        if (shape == 1)
            static_cast<QLabel*>(Control(734))->setText(language_.Text("dialog.physics.depth"));
    }
    if (Body())
        for (int i = 0; i < (shape == 0 ? 1 : shape == 1 ? 3 : 2); ++i)
            Edit(709 + i)->setText(Number("%3.2f", edit_.bodies[body_].size[i]));
    else if (!shape)
        Edit(709)->clear();
}
void ModelPhysicsDialog::FillBody() {
    resetting_ = true;
    for (int id = 705; id <= 731; ++id)
        if (Control(id))
            Control(id)->setEnabled(Body());
    if (!Body()) {
        Edit(705)->clear();
        Combo(707)->setCurrentIndex(1);
        Combo(708)->setCurrentIndex(0);
        Shape(0);
        for (int id = 712; id <= 723; ++id)
            Edit(id)->clear();
        Radio(730)->setChecked(true);
    } else {
        const auto& body = edit_.bodies[body_];
        Edit(705)->setText(Name(body.pmdName));
        Combo(707)->setCurrentIndex(body.boneIndex + 1);
        Combo(708)->setCurrentIndex(body.group);
        Shape(body.shape);
        for (int i = 0; i < 3; ++i) {
            Edit(712 + i)->setText(Number("%3.2f", body.position[i]));
            Edit(715 + i)->setText(Number("%3.2f", body.rotation[i] / 3.141592f * 180.f));
        }
        Edit(718)->setText(Number("%4.3f", body.mass));
        Edit(720)->setText(Number("%4.3f", body.linearDamping));
        Edit(721)->setText(Number("%4.3f", body.angularDamping));
        Edit(722)->setText(Number("%4.3f", body.restitution));
        Edit(723)->setText(Number("%4.3f", body.friction));
        QString mask;
        for (int bit = 0; bit < 16; ++bit)
            if (body.noCollapse & (1u << bit))
                mask += QString::number(bit + 1) + ' ';
        Edit(719)->setText(mask);
        Radio(body.mode ? 729 : 730)->setChecked(true);
        Checkbox(731)->setChecked(body.mode == 2);
        Checkbox(731)->setEnabled(body.mode != 0);
    }
    resetting_ = false;
}
void ModelPhysicsDialog::FillJoint() {
    resetting_ = true;
    for (int id = 737; id <= 767; ++id)
        if (Control(id))
            Control(id)->setEnabled(Joint());
    if (!Joint()) {
        Combo(741)->setCurrentIndex(0);
        Combo(742)->setCurrentIndex(0);
        Combo(743)->setCurrentIndex(-1);
        for (int id = 744; id <= 767; ++id)
            Edit(id)->clear();
    } else {
        const auto& joint = edit_.joints[joint_];
        Edit(740)->setText(Name(joint.pmdName));
        Combo(741)->setCurrentIndex(edit_.BodyOrdinal(joint.rigidA));
        Combo(742)->setCurrentIndex(edit_.BodyOrdinal(joint.rigidB));
        for (int i = 0; i < 3; ++i) {
            Edit(744 + i)->setText(Number("%5.4f", joint.position[i]));
            Edit(754 + i)->setText(Number("%3.2f", joint.rotation[i] / 3.141592f * 180.f));
        }
        for (int i = 0; i < 12; ++i)
            Edit(kLimits[i])
                ->setText(
                    Number("%3.2f", i < 6 ? joint.limits[i] : joint.limits[i] / 3.141592f * 180.f));
        for (int i = 0; i < 6; ++i)
            Edit(748 + i)->setText(Number("%3.2f", joint.springs[i]));
    }
    resetting_ = false;
}
bool ModelPhysicsDialog::CommitField(int id, bool enter) {
    if (!Valid() || closing_)
        return false;
    if (id == 705 || id == 740) {
        if (id == 705 ? !Body() : !Joint())
            return true;
        auto bytes =
            editor::EnglishNameBytes(Edit(id)->text().toUtf8().toStdString(), enter ? 255 : 99);
        if (bytes.size() > 20 && enter)
            bytes.resize(19);
        if (bytes.size() >= 20) {
            if (failed)
                failed("physics-name-too-long");
            return false; // Original's exactly-20-byte CRT overwrite is not a valid edit.
        }
        auto* name = id == 705 ? edit_.bodies[body_].pmdName : edit_.joints[joint_].pmdName;
        std::memset(name, 0, 20);
        std::memcpy(name, bytes.data(), bytes.size());
        if (enter) {
            for (int combo : id == 705 ? std::vector<int>{704, 741, 742} : std::vector<int>{736}) {
                const auto row = Combo(id == 705 ? 704 : 736)->currentIndex();
                Combo(combo)->setItemText(row, Name(name));
            }
        }
        return true;
    }
    if (id == 719) {
        if (Body())
            edit_.bodies[body_].noCollapse = editor::PhysicsCollisionMask(
                editor::EnglishNameBytes(Edit(id)->text().toUtf8().toStdString(), 99));
        return true;
    }
    if (id < 744 ? !Body() : !Joint())
        return true;
    double value;
    if (!NativeDialogDoublePrefix(Edit(id)->text(), enter ? 255 : 9, value)) {
        if (failed)
            failed("physics-number-not-finite");
        return false;
    }
    if (id < 744) {
        if ((id >= 709 && id <= 711 || id == 718 && enter) && value < .1)
            Edit(id)->setText("0.1");
        editor::SetPhysicsBodyValue(edit_.bodies[body_], id, value, enter);
    } else {
        bool clamped;
        editor::SetPhysicsJointValue(edit_.joints[joint_], id, value, clamped, enter);
        if (clamped) {
            Edit(id)->setText(value > 0 ? "80.00" : "-80.00");
            if (report)
                report(language_.Text("dialog.physics.angle_title"),
                       language_.Text("dialog.physics.angle_limit"));
        }
    }
    return true;
}
bool ModelPhysicsDialog::Collect(bool body) {
    if (body ? !Body() : !Joint())
        return true;
    if (!CommitField(body ? 705 : 740, false))
        return false;
    for (int id = body ? 709 : 744; id <= (body ? 723 : 767); ++id)
        if (!CommitField(id, false))
            return false;
    return true;
}
void ModelPhysicsDialog::Select(int id, int row) {
    if (resetting_ || closing_ || !Valid() || row < 0)
        return;
    if (id == 704 || id == 736) {
        if (!Collect(id == 704))
            return;
        (id == 704 ? body_ : joint_) = id == 704 ? edit_.BodyAtRow(row) : edit_.JointAtRow(row);
        id == 704 ? FillBody() : FillJoint();
    } else if (id == 707 && Body())
        edit_.bodies[body_].boneIndex = row - 1;
    else if (id == 708 && Body())
        edit_.bodies[body_].group = std::uint8_t(row);
    else if ((id == 741 || id == 742) && Joint())
        (id == 741 ? edit_.joints[joint_].rigidA : edit_.joints[joint_].rigidB) =
            edit_.BodyAtRow(row);
    else if (id == 743 && Joint()) {
        const auto& bone = resource_->PmdSource()->bones.at(unsigned(row));
        std::copy_n(bone.position, 3, edit_.joints[joint_].position);
        for (int i = 0; i < 3; ++i)
            Edit(744 + i)->setText(Number("%5.4f", bone.position[i]));
    }
}
void ModelPhysicsDialog::ButtonClicked(int id) {
    if (closing_ || !Valid())
        return;
    if (id == 1 || id == 2) {
        Finish(id == 1);
        return;
    }
    if (id == 685 || id == 735) {
        if (!Collect(id == 685))
            return;
        const int added = id == 685 ? edit_.AddBody() : edit_.AddJoint();
        if (added < 0) {
            if (report)
                report(language_.Text("dialog.physics.add_title"),
                       language_.Text(edit_.bodyOrder.empty() ? "dialog.physics.no_body"
                                                              : "dialog.physics.capacity"));
            return;
        }
        (id == 685 ? body_ : joint_) = added;
        for (int list : id == 685 ? std::vector<int>{704, 741, 742} : std::vector<int>{736}) {
            auto* combo = Combo(list);
            QSignalBlocker block(combo);
            const int previous = combo->currentIndex();
            combo->addItem(
                Name(id == 685 ? edit_.bodies[added].pmdName : edit_.joints[added].pmdName), added);
            combo->setCurrentIndex(list == 704 || list == 736 ? combo->count() - 1 : previous);
        }
        id == 685 ? FillBody() : FillJoint();
    } else if ((id == 706 && Body()) || (id == 737 && Joint())) {
        const auto oldJoints = edit_.jointOrder;
        const int row = Combo(id == 706 ? 704 : 736)->currentIndex();
        id == 706 ? edit_.DeleteBody(body_, Combo(704)->currentIndex())
                  : edit_.DeleteJoint(joint_, Combo(736)->currentIndex());
        if (id == 706) {
            for (int list : {704, 741, 742}) {
                QSignalBlocker block(Combo(list));
                Combo(list)->removeItem(row);
                Combo(list)->setCurrentIndex(list == 704 && Combo(list)->count() ? 0 : -1);
            }
            QSignalBlocker block(Combo(736));
            for (int i = int(oldJoints.size()); i-- > 0;)
                if (std::find(edit_.jointOrder.begin(), edit_.jointOrder.end(), oldJoints[i]) ==
                    edit_.jointOrder.end())
                    Combo(736)->removeItem(i);
            Combo(736)->setCurrentIndex(Combo(736)->count() ? 0 : -1);
        } else {
            QSignalBlocker block(Combo(736));
            Combo(736)->removeItem(row);
            Combo(736)->setCurrentIndex(Combo(736)->count() ? 0 : -1);
        }
        // Original only changes the scratch selection when slot zero is live.
        if (id == 706) {
            if (edit_.bodyOrder.empty())
                body_ = -1;
            else if (edit_.BodyOrdinal(0) == 0)
                body_ = 0;
        }
        if (edit_.jointOrder.empty())
            joint_ = -1;
        else if (edit_.JointOrdinal(0) == 0)
            joint_ = 0;
        if (id == 706)
            FillBody();
        FillJoint();
    } else if (id == 724 && Body()) {
        if (Collect(true))
            session_.PhysicsClipboard().body = edit_.bodies[body_];
    } else if (id == 725 && Body()) {
        edit_.PasteBody(body_, session_.PhysicsClipboard());
        FillBody();
    } else if (id == 738 && Joint()) {
        if (Collect(false))
            session_.PhysicsClipboard().joint = edit_.joints[joint_];
    } else if (id == 739 && Joint()) {
        edit_.PasteJoint(joint_, session_.PhysicsClipboard());
        FillJoint();
    } else if (id >= 726 && id <= 728 && Body()) {
        edit_.bodies[body_].shape = std::uint8_t(id - 726);
        Shape(id - 726);
    } else if ((id == 729 || id == 730) && Body()) {
        edit_.bodies[body_].mode = id == 730 ? 0 : Checkbox(731)->isChecked() ? 2 : 1;
        Checkbox(731)->setEnabled(id == 729);
    } else if (id == 731 && Body()) {
        auto& mode = edit_.bodies[body_].mode;
        if (mode)
            mode = mode == 2 ? 1 : 2;
    } else if (id == 800 || id == 801) {
        if (Collect(id == 801))
            Page(id == 800);
    }
}
bool ModelPhysicsDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            const auto id = watched->property("mmdControlId").toInt();
            if (id == 719)
                accept(); // No subclass handler; native default button receives Enter.
            else
                CommitField(id, true);
            return true;
        }
    }
    return TemplateDialog::eventFilter(watched, event);
}
void ModelPhysicsDialog::Finish(bool accepted) {
    if (closing_)
        return;
    if (!Valid()) {
        Invalidate();
        return;
    }
    if (accepted && (!Collect(true) || !Collect(false)))
        return;
    std::string reason;
    if (!session_.FinishModelPhysicsEdit(slot_, accepted ? &edit_ : nullptr, reason)) {
        if (failed)
            failed(reason);
        return;
    }
    closing_ = true;
    if (changed)
        changed();
    done(accepted ? QDialog::Accepted : QDialog::Rejected);
    if (accepted && preserve)
        preserve();
}
void ModelPhysicsDialog::accept() {
    Finish(true);
}
void ModelPhysicsDialog::reject() {
    Finish(false);
}
void ModelPhysicsDialog::closeEvent(QCloseEvent* event) {
    Finish(false);
    event->setAccepted(closing_);
}
}
