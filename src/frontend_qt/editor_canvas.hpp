#pragma once
#include "language.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <QImage>
#include <QWidget>

namespace mikudancestudio::qt {
class Canvas final : public QWidget {
public:
    Canvas(Language&, editor::EditorSession&, const std::filesystem::path& assets, QWidget* parent,
           bool viewportOnly = false);
    QString ViewportTitle() const;
    void SetDetached(bool);
    void SetFullScreen(bool);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Language& language_;
    editor::EditorSession& session_;
    QImage viewTools_;
    bool viewportOnly_ = false, detached_ = false, fullScreen_ = false;
};
}
