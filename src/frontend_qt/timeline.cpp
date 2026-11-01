#include "timeline.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
namespace mikudancestudio::qt {
using namespace editor;
Timeline::Timeline(EditorSession& s, Language& l, QWidget* p)
    : QWidget(p), session_(s), language_(l) {
    setObjectName("mmd.timeline");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}
void Timeline::Refresh() {
    rows_.clear();
    const auto model = session_.Selected().model;
    if (model == 255) {
        for (auto pair : {std::pair{Track::Camera, "camera"},
                          {Track::Light, "light"},
                          {Track::Shadow, "shadow"},
                          {Track::Gravity, "gravity"}})
            rows_.push_back({{pair.first, 255, 0, 0}, language_.Text(pair.second)});
        for (const auto& a : session_.Document().accessories) {
            std::string name;
            text::DecodeCp932(
                {a.name.data(),
                 std::size_t(std::find(a.name.begin(), a.name.end(), 0) - a.name.begin())},
                name);
            rows_.push_back({{Track::Accessory, a.slot, 0, 0}, QString::fromUtf8(name.c_str())});
        }
    } else
        for (const auto& m : session_.Document().models)
            if (m.slot == model) {
                const auto names = [&](const auto& names, Track track) {
                    for (std::size_t i = 0; i < names.size(); ++i) {
                        std::string decoded;
                        text::DecodeCp932({names[i].data(), names[i].size()}, decoded);
                        rows_.push_back(
                            {{track, model, unsigned(i), 0}, QString::fromUtf8(decoded.c_str())});
                    }
                };
                rows_.push_back({{Track::Display, model, 0, 0}, language_.Text("display")});
                names(m.boneNames, Track::Bone);
                names(m.morphNames, Track::Morph);
            }
    rowScroll_ = std::clamp(rowScroll_, 0, std::max(0, int(rows_.size()) - 1));
    update();
}
void Timeline::SetFrameScroll(int value) {
    frameScroll_ = std::clamp(value, 0, 2147483000);
    update();
}
void Timeline::SetRowScroll(int value) {
    rowScroll_ = std::clamp(value, 0, std::max(0, int(rows_.size()) - 1));
    update();
}
int Timeline::FrameAt(int x) const {
    return std::max(0, frameScroll_ + int(std::floor((x - 100 + 6.5) / 13.)));
}
int Timeline::RowAt(int y) const {
    return rowScroll_ + (y - 16) / 14;
}
void Timeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(240, 240, 240));
    QFont font = this->font();
    font.setPixelSize(12);
    p.setFont(font);
    for (int i = 0; i < int(rows_.size()) - rowScroll_ && 16 + i * 14 < height(); ++i) {
        const auto y = 16 + i * 14;
        p.fillRect(91, y, width() - 91, 13, i % 2 ? QColor(242, 242, 226) : QColor(224, 238, 244));
        p.setPen(Qt::black);
        p.drawText(QRect(0, y, 89, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   rows_[rowScroll_ + i].name);
    }
    const auto cursor = session_.Document().timeline.currentFrame;
    for (int x = 100, index = 0; x < width(); x += 13, ++index) {
        const auto frame = frameScroll_ + index;
        p.setPen(frame == cursor ? QColor(255, 0, 0)
                 : frame % 5     ? QColor(210, 210, 210)
                                 : QColor(135, 135, 135));
        p.drawLine(x, 16, x, height());
        if (frame % 5 == 0) {
            p.setPen(Qt::black);
            p.drawText(x - 3, 12, QString::number(frame));
        }
    }
    const auto keys = session_.Keys();
    for (const auto& key : keys)
        for (std::size_t row = std::size_t(rowScroll_); row < rows_.size(); ++row) {
            const auto& a = rows_[row].address;
            if (key.track != a.track || key.slot != a.slot || key.channel != a.channel)
                continue;
            const int x = 100 + (int(key.frame) - frameScroll_) * 13,
                      y = 23 + (int(row) - rowScroll_) * 14;
            if (x < 91 || x >= width() || y >= height())
                continue;
            p.setPen(Qt::black);
            p.setBrush(session_.Selected().keys.count(key) ? QColor(255, 60, 60)
                                                           : QColor(60, 120, 205));
            p.drawPolygon(
                QPolygon({QPoint(x, y - 5), QPoint(x + 5, y), QPoint(x, y + 5), QPoint(x - 5, y)}));
        }
    if (dragging_) {
        p.setPen(QPen(QColor(100, 100, 100), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(press_, pointer_).normalized());
    }
}
void Timeline::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    press_ = pointer_ = e->position().toPoint();
    setFocus();
    auto& selection = session_.Selected();
    const int row = RowAt(press_.y());
    if (press_.x() < 91 && row >= 0 && row < int(rows_.size())) {
        const auto& a = rows_[row].address;
        if (a.track == Track::Bone) {
            if (!(e->modifiers() & Qt::ControlModifier))
                selection.bones.clear();
            selection.bones.push_back(a.channel);
        }
        if (changed)
            changed();
        return;
    }
    const auto frame = unsigned(FrameAt(press_.x()));
    if (!(e->modifiers() & Qt::ControlModifier))
        selection.keys.clear();
    if (row >= 0 && row < int(rows_.size()) && press_.y() >= 16) {
        auto a = rows_[row].address;
        a.frame = frame;
        const auto keys = session_.Keys();
        if (std::find_if(keys.begin(), keys.end(),
                         [&](const auto& key) { return !(a < key) && !(key < a); }) != keys.end())
            selection.keys.insert(a);
    }
    std::string error;
    session_.Seek(frame, error);
    dragging_ = true;
    if (changed)
        changed();
    update();
}
void Timeline::mouseMoveEvent(QMouseEvent* event) {
    pointer_ = event->position().toPoint();
    if (dragging_)
        update();
}
void Timeline::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_)
        return;
    dragging_ = false;
    const auto end = e->position().toPoint();
    if ((end - press_).manhattanLength() > 4) {
        const auto r = QRect(press_, end).normalized();
        auto& selected = session_.Selected().keys;
        const auto keys = session_.Keys();
        for (int row = std::max(0, RowAt(r.top()));
             row <= RowAt(r.bottom()) && row < int(rows_.size()); ++row)
            for (const auto& key : keys) {
                auto a = rows_[row].address;
                a.frame = key.frame;
                if (!(a < key) && !(key < a) && key.frame >= unsigned(FrameAt(r.left())) &&
                    key.frame <= unsigned(FrameAt(r.right())))
                    selected.insert(key);
            }
    }
    if (changed)
        changed();
    update();
}
void Timeline::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ShiftModifier)
        SetFrameScroll(frameScroll_ - e->angleDelta().y() / 120 * 3);
    else
        SetRowScroll(rowScroll_ - e->angleDelta().y() / 120 * 3);
    e->accept();
}
std::vector<KeyAddress> Timeline::CurrentTracks() const {
    std::vector<KeyAddress> result;
    for (const auto& r : rows_) {
        auto a = r.address;
        a.frame = unsigned(session_.Document().timeline.currentFrame);
        result.push_back(a);
    }
    return result;
}
void Timeline::SelectAll() {
    SelectRange(0, 0x7fffffff);
}
void Timeline::SelectRange(unsigned first, unsigned last) {
    auto& selected = session_.Selected().keys;
    selected.clear();
    for (const auto& key : session_.Keys())
        for (const auto& r : rows_) {
            const auto& a = r.address;
            if (key.track == a.track && key.slot == a.slot && key.channel == a.channel &&
                key.frame >= first && key.frame <= last)
                selected.insert(key);
        }
    update();
}
void Timeline::SelectColumn() {
    const auto f = unsigned(session_.Document().timeline.currentFrame);
    SelectRange(f, f);
}
void Timeline::SelectTrack(Track track) {
    SelectAll();
    auto& keys = session_.Selected().keys;
    for (auto it = keys.begin(); it != keys.end();)
        if (it->track != track)
            it = keys.erase(it);
        else
            ++it;
    update();
}
}
