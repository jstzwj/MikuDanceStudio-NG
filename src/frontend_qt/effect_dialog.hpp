#pragma once
#include "language.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <QDialog>
#include <functional>
class QComboBox;
class QLineEdit;
class QCheckBox;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
namespace mikudancestudio::qt {
class EffectDialog final : public QDialog {
public:
    using Inspect =
        std::function<std::shared_ptr<const mme::EffectDefinition>(editor::EffectAddress)>;
    EffectDialog(editor::EditorSession&, Language&, Inspect, std::function<std::string()>,
                 std::function<void()>, QWidget*);
    void Refresh();
    void Retranslate();

private:
    editor::EditorSession& session_;
    Language& language_;
    Inspect inspect_;
    std::function<std::string()> report_;
    std::function<void()> changed_;
    QComboBox *objects_, *subsets_;
    QLineEdit* path_;
    QCheckBox *enabled_, *basic_;
    QTableWidget* parameters_;
    QPlainTextEdit* reportBox_;
    QPushButton *browse_, *remove_, *reload_, *undo_, *redo_;
    bool refreshing_ = false;
    std::uint64_t revision_ = 0;
    std::map<std::string, mme::ParameterValue> values_;
    editor::EffectAddress Address() const;
    void ObjectChanged();
    void SelectionChanged();
    void Apply(bool, const std::string&);
};
}
