#pragma once
#include "numeric_dialog.hpp"
#include "mikudancestudio/editor_session.hpp"
namespace mikudancestudio::qt {
class ToonDialog final : public TemplateDialog {
public:
    ToonDialog(editor::EditorSession&, const Language&, QWidget* = nullptr);
    QLineEdit* Field(unsigned i) const { return Edit(709 + int(i)); }
    void accept() override;
    std::function<void(const QString&)> missing;

private:
    editor::EditorSession& session_;
    std::uint8_t slot_;
    std::shared_ptr<const runtime::ModelResource> resource_;
};
}
