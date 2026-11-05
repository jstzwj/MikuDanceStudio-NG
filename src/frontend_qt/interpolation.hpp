#pragma once
#include "mikudancestudio/editor_session.hpp"
#include <QWidget>
#include <QImage>
namespace mikudancestudio::qt {
QImage InterpolationImage(const editor::InterpolationSelection&);
class Interpolation final : public QWidget {
public:
    Interpolation(editor::EditorSession&, QWidget*);
    void SetChannel(unsigned);
    void SetDisplayScale(double);
    unsigned Channel() const { return channel_; }
    std::function<void(bool, const std::string&)> changed;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    editor::EditorSession& session_;
    unsigned channel_ = 3;
    int point_ = -1;
    double displayScale_ = 1;
    void End();
};
}
