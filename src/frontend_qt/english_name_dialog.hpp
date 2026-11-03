#pragma once
#include "numeric_dialog.hpp"
#include "mikudancestudio/editor_session.hpp"

namespace mikudancestudio::qt {
class EnglishNameDialog final : public TemplateDialog {
public:
    EnglishNameDialog(editor::EditorSession&, const Language&, QWidget* = nullptr);
    bool Valid() const;
    bool Active() const { return !closing_ && Valid(); }
    void Invalidate();
    void accept() override;
    void reject() override;
    std::function<void()> changed, preserve;
    std::function<void(const std::string&)> failed;

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void ButtonClicked(int) override;
    void closeEvent(QCloseEvent*) override;

private:
    editor::EditorSession& session_;
    std::uint8_t slot_;
    std::shared_ptr<const runtime::ModelResource> resource_;
    std::array<int, 3> cursors_{};
    unsigned firstGroup_ = 2;
    bool hasFacial_ = false, closing_ = false;
    bool Commit(unsigned);
    void Select(unsigned, int);
    void Finish(bool);
    void SelectBone(unsigned);
};
}
