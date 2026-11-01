#pragma once
#include "language.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <QWidget>
#include <QImage>
#include <functional>
namespace mikudancestudio::qt {
class Timeline final : public QWidget {
public:
    Timeline(editor::EditorSession&, Language&, const std::filesystem::path& assets, QWidget*);
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
        std::vector<editor::KeyAddress> tracks;
        int group = -1, indent = 12;
        bool expanded = false;
        Row(editor::KeyAddress a, QString n) : address(a), name(std::move(n)), tracks{a} {}
    };
    editor::EditorSession& session_;
    Language& language_;
    std::vector<Row> rows_;
    std::weak_ptr<const runtime::ModelResource> rowsResource_;
    std::uint8_t rowsModel_ = 255;
    QString rowsLanguage_;
    std::vector<std::uint8_t> rowsGroupFlags_;
    int frameScroll_ = 0, rowScroll_ = 0;
    QPoint press_, pointer_;
    bool dragging_ = false;
    QImage icons_;
    bool Matches(const Row&, const editor::KeyAddress&) const;
    bool Selected(const Row&) const;
    int FrameAt(int x) const;
    int RowAt(int y) const;
};
}
