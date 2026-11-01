#include "timeline.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/mmd_editor_chrome.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
namespace mikudancestudio::qt {
using namespace editor;
Timeline::Timeline(EditorSession& s, Language& l, const std::filesystem::path& assets, QWidget* p)
    : QWidget(p), session_(s), language_(l) {
    icons_.load(QString::fromUtf8((assets / "sidebar_icon_sheet.bmp").u8string().c_str()));
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
                const auto resource = session_.Runtime().Resource(model);
                if (!resource)
                    continue;
                const auto name = [&](const runtime::ModelDisplayEntry& entry) {
                    const auto& names = entry.morph ? resource->Document().morphs[entry.index].names
                                                    : resource->Document().bones[entry.index].names;
                    const auto& bytes =
                        language_.Code() == "en" && !names[1].empty() ? names[1] : names[0];
                    std::string decoded;
                    text::DecodeUtf16Le(bytes.data(), bytes.size(), decoded);
                    return QString::fromUtf8(decoded.c_str());
                };
                const auto& groups = resource->DisplayGroups();
                for (std::size_t i = 0; i < groups.size(); ++i) {
                    const auto& group = groups[i];
                    if (group.role == 0) {
                        if (!group.entries.empty())
                            rows_.push_back({{Track::Bone, model, group.entries[0].index, 0},
                                             name(group.entries[0])});
                    } else if (group.role == 1) {
                        rows_.push_back({{Track::Display, model, 0, 0}, language_.Text("display")});
                    } else {
                        const auto text =
                            group.role == 2
                                ? language_.Text("morph")
                                : QString::fromUtf8(
                                      group
                                          .names[language_.Code() == "en" && !group.names[1].empty()
                                                     ? 1
                                                     : 0]
                                          .c_str());
                        Row header({Track::Display, model, 0, 0}, text);
                        header.tracks.clear();
                        header.group = int(i);
                        header.expanded =
                            i < m.selection.groupFlags.size() && m.selection.groupFlags[i];
                        if (!header.expanded)
                            for (const auto& entry : group.entries)
                                header.tracks.push_back({entry.morph ? Track::Morph : Track::Bone,
                                                         model, entry.index, 0});
                        rows_.push_back(header);
                        if (header.expanded)
                            for (const auto& entry : group.entries) {
                                Row row({entry.morph ? Track::Morph : Track::Bone, model,
                                         entry.index, 0},
                                        name(entry));
                                row.indent = 15;
                                rows_.push_back(std::move(row));
                            }
                    }
                }
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
    return y < 16 ? -1 : rowScroll_ + (y - 16) / 14;
}
bool Timeline::Matches(const Row& row, const KeyAddress& key) const {
    return std::any_of(row.tracks.begin(), row.tracks.end(), [&](const auto& a) {
        return a.track == key.track && a.slot == key.slot && a.channel == key.channel;
    });
}
bool Timeline::Selected(const Row& row) const {
    const auto& selected = session_.Selected();
    for (const auto& a : row.tracks) {
        if (a.track == Track::Bone && std::find(selected.bones.begin(), selected.bones.end(),
                                                a.channel) != selected.bones.end())
            return true;
        if (selected.tracks.count(a))
            return true;
    }
    return false;
}
void Timeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto color = [](unsigned n) {
        const auto v = ui::kDefaultThemeColors[n];
        return QColor(v & 255, (v >> 8) & 255, (v >> 16) & 255);
    };
    p.fillRect(rect(), color(22));
    p.fillRect(1, 15, 89, height() - 15, color(27));
    p.fillRect(90, 1, width() - 90, 14, color(27));
    for (int y = 15; y < height(); y += 28) {
        p.fillRect(1, y, 89, 15, color(28));
        p.fillRect(91, y, width() - 91, 15, color(23));
    }
    const auto corner =
        ui::ColorLerp(ui::kDefaultThemeColors[2], ui::kDefaultThemeColors[3], .08281938f);
    p.fillRect(0, 0, 89, 15, QColor(corner & 255, (corner >> 8) & 255, (corner >> 16) & 255));
    for (int y = 15; y < height(); y += 14) {
        p.setPen(color(29));
        p.drawLine(1, y, 89, y);
        p.setPen(color(24));
        p.drawLine(91, y, width() - 1, y);
    }
    QFont font = this->font();
    font.setPixelSize(12);
    p.setFont(font);
    for (int i = 0; i < int(rows_.size()) - rowScroll_ && 16 + i * 14 < height(); ++i) {
        const auto& row = rows_[rowScroll_ + i];
        const auto y = 16 + i * 14;
        const auto selected = Selected(row);
        if (selected)
            p.fillRect(91, y, width() - 91, 13, color(i % 2 ? 25 : 26));
        p.setPen(color(selected ? 34 : 33));
        p.save();
        p.setClipRect(0, y, 90, 14);
        p.drawText(QRect(row.indent, y + 1, 90 - row.indent, 14), Qt::AlignLeft | Qt::AlignTop,
                   row.name);
        if (row.group >= 0) {
            p.setPen(Qt::black);
            p.drawText(QRect(1, y + 1, 11, 14), Qt::AlignLeft | Qt::AlignTop,
                       row.expanded ? "-" : "+");
        }
        p.restore();
    }
    const auto cursor = session_.Document().timeline.currentFrame;
    for (int x = 100, index = 0; x < width(); x += 13, ++index) {
        const auto frame = frameScroll_ + index;
        p.setPen(color(frame == cursor ? 30 : frame % 5 ? 32 : 31));
        p.drawLine(x, 16, x, height() - 1);
        if (frame == cursor || frame % 5 == 0) {
            p.setPen(color(frame == cursor ? 30 : 33));
            p.drawText(QRect(x - 3, 3, 70, 14), Qt::AlignLeft | Qt::AlignTop,
                       QString::number(frame));
        }
    }
    const auto keys = session_.Keys();
    for (std::size_t row = std::size_t(rowScroll_); row < rows_.size(); ++row) {
        const auto& r = rows_[row];
        const int y = 17 + (int(row) - rowScroll_) * 14;
        if (y >= height())
            break;
        std::map<unsigned, bool> frames;
        for (const auto& key : keys)
            if (Matches(r, key))
                frames[key.frame] |= session_.Selected().keys.count(key) != 0;
        for (const auto& mark : frames) {
            const auto x64 = 95 + (std::int64_t(mark.first) - frameScroll_) * 13;
            if (x64 < 91 || x64 >= width() || icons_.isNull())
                continue;
            const int sx = r.group >= 0 ? (mark.second ? 33 : 11) : (mark.second ? 22 : 0);
            QImage icon(11, 11, QImage::Format_ARGB32);
            for (int iy = 0; iy < 11; ++iy)
                for (int ix = 0; ix < 11; ++ix) {
                    const auto mask = icons_.pixel(44 + ix, iy);
                    const auto pixel = icons_.pixel(sx + ix, iy);
                    icon.setPixel(ix, iy,
                                  (pixel & 0xffffffu) | (qRed(mask) == 0 ? 0xff000000u : 0));
                }
            p.drawImage(int(x64), y, icon);
        }
    }
    if (dragging_) {
        p.setPen(QColor(150, 150, 150));
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
        const auto selectedRow = rows_[row];
        if (press_.x() < 12 && selectedRow.group >= 0) {
            session_.SetGroupExpanded(selection.model, unsigned(selectedRow.group),
                                      !selectedRow.expanded);
            Refresh();
        } else {
            if (!(e->modifiers() & Qt::ControlModifier)) {
                selection.bones.clear();
                selection.tracks.clear();
            }
            for (const auto& a : selectedRow.tracks) {
                if (a.track == Track::Bone) {
                    const auto found =
                        std::find(selection.bones.begin(), selection.bones.end(), a.channel);
                    if (found == selection.bones.end())
                        selection.bones.push_back(a.channel);
                    else if (e->modifiers() & Qt::ControlModifier)
                        selection.bones.erase(found);
                } else {
                    if (!selection.tracks.erase(a))
                        selection.tracks.insert(a);
                    if (a.track == Track::Accessory)
                        selection.accessory = a.slot;
                }
            }
            if (selection.model != 255) {
                int active = -1;
                for (const auto& model : session_.Document().models)
                    if (model.slot == selection.model)
                        active = model.selection.selectedBone;
                if (selectedRow.address.track == Track::Bone && selectedRow.group < 0)
                    active = std::find(selection.bones.begin(), selection.bones.end(),
                                       selectedRow.address.channel) != selection.bones.end()
                                 ? int(selectedRow.address.channel)
                                 : -1;
                std::string reason;
                session_.SelectBones(selection.bones, active, reason);
            }
        }
        if (changed)
            changed();
        return;
    }
    const auto frame = unsigned(FrameAt(press_.x()));
    if (!(e->modifiers() & Qt::ControlModifier))
        selection.keys.clear();
    if (row >= 0 && row < int(rows_.size()) && press_.y() >= 16) {
        const auto keys = session_.Keys();
        for (const auto& key : keys)
            if (key.frame == frame && Matches(rows_[row], key)) {
                if ((e->modifiers() & Qt::ControlModifier) && selection.keys.count(key))
                    selection.keys.erase(key);
                else
                    selection.keys.insert(key);
            }
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
                if (Matches(rows_[row], key) && key.frame >= unsigned(FrameAt(r.left())) &&
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
        for (auto a : r.tracks) {
            a.frame = unsigned(session_.Document().timeline.currentFrame);
            result.push_back(a);
        }
    }
    return result;
}
void Timeline::SelectAll() {
    SelectRange(0, 0x7fffffff);
}
void Timeline::SelectRange(unsigned first, unsigned last) {
    session_.SelectKeys(first, last, {});
    update();
}
void Timeline::SelectColumn() {
    session_.ExtendSelectedColumns();
    update();
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
