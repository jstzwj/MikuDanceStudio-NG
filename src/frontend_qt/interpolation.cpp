#include "interpolation.hpp"
#include <QPainter>
#include <QMouseEvent>
namespace mikudancestudio::qt {
QImage InterpolationImage(const editor::InterpolationSelection& selected) {
    QImage image(128, 128, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    const auto line = [&](QPoint first, QPoint last) {
        // GDI integer line rule: nearest minor-axis pixel, half ties toward
        // the smaller coordinate, with LineTo's destination excluded.
        if (first == last)
            return;
        const auto excluded = last;
        const bool steep = std::abs(last.y() - first.y()) > std::abs(last.x() - first.x());
        if (steep) {
            first = {first.y(), first.x()};
            last = {last.y(), last.x()};
        }
        if (first.x() > last.x())
            std::swap(first, last);
        const int major = last.x() - first.x(), minor = last.y() - first.y();
        for (int i = 0; i <= major; ++i) {
            const int numerator = 2 * minor * i + major - 1;
            const int denominator = 2 * major;
            const int rounded = numerator >= 0 ? numerator / denominator
                                               : -((-numerator + denominator - 1) / denominator);
            const QPoint pixel = steep ? QPoint(first.y() + rounded, first.x() + i)
                                       : QPoint(first.x() + i, first.y() + rounded);
            if (pixel != excluded)
                painter.fillRect(QRect(pixel, QSize(1, 1)), painter.pen().color());
        }
    };
    painter.setPen(QPen(Qt::blue, 1));
    for (const auto& c : selected.curves) {
        QPoint previous(0, 126);
        for (int step = 1; step < 100; ++step) {
            const float t = float(step) / 100.f;
            const float ax = c[0] * t, ay = c[1] * t;
            const float bx = c[0] + (c[2] - c[0]) * t, by = c[1] + (c[3] - c[1]) * t;
            const float cx = c[2] + (128 - c[2]) * t, cy = c[3] + (128 - c[3]) * t;
            const float dx = ax + (bx - ax) * t, dy = ay + (by - ay) * t;
            const float ex = bx + (cx - bx) * t, ey = by + (cy - by) * t;
            const QPoint next(int(dx + (ex - dx) * t), 126 - int(dy + (ey - dy) * t));
            line(previous, next);
            previous = next;
        }
    }
    if (selected.uniform) {
        const auto& c = selected.curves.front();
        // The original two-pixel GDI diagonal pen has this integer footprint.
        // Qt's geometric stroker uses different cap/edge rounding even with
        // antialiasing disabled, so preserve the 7x7 control marker explicitly.
        constexpr const char* cross[]{"##...##", "###.###", ".#####.", "..###..",
                                      ".#####.", "###.###", "##...##"};
        for (unsigned i = 0; i < 4; i += 2) {
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 7; ++x)
                    if (cross[y][x] == '#')
                        painter.fillRect(c[i] - 3 + x, 124 - c[i + 1] + y, 1, 1, Qt::red);
        }
        painter.setPen(QPen(Qt::black, 1));
        line({0, 127}, {c[0], 127 - c[1]});
        line({127, 0}, {c[2], 127 - c[3]});
    }
    return image;
}
Interpolation::Interpolation(editor::EditorSession& session, QWidget* parent)
    : QWidget(parent), session_(session) {
    setObjectName("mmd.interpolation");
    setFixedSize(128, 128);
}
void Interpolation::SetChannel(unsigned channel) {
    if (channel != channel_) {
        End();
        channel_ = channel;
    }
    update();
}
void Interpolation::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.drawImage(0, 0, InterpolationImage(session_.Interpolation(channel_)));
}
void Interpolation::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || session_.Playing())
        return;
    const auto selection = session_.Interpolation(channel_);
    if (!selection.uniform)
        return;
    const auto& c = selection.curves.front();
    for (int i = 0; i < 2; ++i)
        if (std::abs(event->position().x() - c[2 * i]) < 5 &&
            std::abs(event->position().y() - (127 - c[2 * i + 1])) < 5) {
            if (session_.BeginGesture())
                point_ = i;
            break;
        }
}
void Interpolation::mouseMoveEvent(QMouseEvent* event) {
    if (point_ < 0)
        return;
    std::string reason;
    const bool ok =
        session_.MoveInterpolationPoint(channel_, unsigned(point_), int(event->position().x()),
                                        127 - int(event->position().y()), reason);
    if (!ok)
        End();
    if (changed)
        changed(ok, reason);
    update();
}
void Interpolation::End() {
    if (point_ >= 0)
        session_.EndGesture();
    point_ = -1;
}
void Interpolation::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mouseMoveEvent(event);
        End();
    }
}
void Interpolation::hideEvent(QHideEvent*) {
    End();
}
}
