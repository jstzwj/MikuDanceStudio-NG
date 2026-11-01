#pragma once
#include "language.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <QWidget>
#include <functional>
namespace mikudancestudio::qt {
class Timeline final : public QWidget {
public:
    Timeline(editor::EditorSession&, Language&, QWidget*);
    std::function<void()> changed;
    void Refresh();
    void SetFrameScroll(int);
    void SetRowScroll(int);
    int FrameScroll() const { return frameScroll_; }
    int RowCount() const { return int(rows_.size()); }
    void SelectAll();
    void SelectRange(unsigned first, unsigned last);
    void SelectColumn();
    void SelectTrack(editor::Track);
    std::vector<editor::KeyAddress> CurrentTracks() const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct Row {
        editor::KeyAddress address;
        QString name;
    };
    editor::EditorSession& session_;
    Language& language_;
    std::vector<Row> rows_;
    int frameScroll_ = 0, rowScroll_ = 0;
    QPoint press_;
    bool dragging_ = false;
    int FrameAt(int x) const;
    int RowAt(int y) const;
};
}
