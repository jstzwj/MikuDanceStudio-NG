#pragma once
#include "numeric_dialog.hpp"
#include "mikudancestudio/editor_session.hpp"

namespace mikudancestudio::qt {
class ModelPhysicsDialog final : public TemplateDialog {
public:
    ModelPhysicsDialog(editor::EditorSession&, const Language&, QWidget* = nullptr);
    bool Valid() const;
    bool Active() const { return !closing_ && Valid(); }
    void Invalidate();
    void accept() override;
    void reject() override;
    const editor::ModelPhysicsEdit& State() const { return edit_; }
    int SelectedBody() const { return body_; }
    int SelectedJoint() const { return joint_; }
    bool JointPage() const { return !Radio(800)->isChecked(); }
    bool Drag(unsigned mode, int dy, bool shift);
    QWidget* Control(int id) const;
    std::function<void()> changed, preserve;
    std::function<void(const std::string&)> failed;
    std::function<void(const QString&, const QString&)> report;

protected:
    void ButtonClicked(int) override;
    bool eventFilter(QObject*, QEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    editor::EditorSession& session_;
    Language language_;
    std::uint8_t slot_;
    std::shared_ptr<const runtime::ModelResource> resource_;
    editor::ModelPhysicsEdit edit_;
    int body_ = -1, joint_ = -1;
    bool closing_ = false, resetting_ = false;
    bool Body() const;
    bool Joint() const;
    bool Collect(bool body);
    bool CommitField(int, bool enter);
    void FillBody();
    void FillJoint();
    void Shape(int);
    void Page(bool body);
    void Lists();
    void Select(int, int);
    void Finish(bool);
};
}
