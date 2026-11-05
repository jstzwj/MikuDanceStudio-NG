#include "editor_canvas.hpp"
#include "editor_window.hpp"
#include "editor_font.hpp"
#include "model_labels.hpp"
#include "mikudancestudio/mmd_editor_labels.hpp"
#include <QPainter>

namespace mikudancestudio::qt {
Canvas::Canvas(Language& language, editor::EditorSession& session,
               const std::filesystem::path& assets, QWidget* p, bool viewportOnly)
    : QWidget(p), language_(language), session_(session),
      viewTools_(Path(assets / "playing_indicator.bmp")), viewportOnly_(viewportOnly) {
    setAutoFillBackground(true);
    setMouseTracking(true);
    setProperty("mmdEditorCanvas", true);
}
void Canvas::SetDetached(bool value) {
    detached_ = value;
    update();
}
void Canvas::SetFullScreen(bool value) {
    fullScreen_ = value;
    update();
}
QString Canvas::ViewportTitle() const {
    const bool english = language_.EnglishLayout();
    QString title = language_.Text("viewport.camera_light_accessory");
    if (session_.Playing())
        title = language_.Text("viewport.playing");
    else if (session_.Selected().model == 255) {
        for (const auto& accessory : session_.Document().accessories)
            if (accessory.slot == session_.Selected().accessory) {
                const auto end = std::find(accessory.name.begin(), accessory.name.end(), 0);
                std::string decoded;
                text::DecodeCp932(
                    {accessory.name.data(), std::size_t(end - accessory.name.begin())}, decoded);
                title += " / " + QString::fromUtf8(decoded.c_str());
            }
    } else {
        const auto slot = session_.Selected().model;
        title = ModelDisplayLabel(session_, slot, english);
        for (const auto& model : session_.Document().models)
            if (model.slot == slot && model.selection.selectedBone >= 0) {
                const auto bone = ModelTrackLabel(
                    session_, slot, unsigned(model.selection.selectedBone), false, english, true);
                title += (english ? QStringLiteral(" : ") : QString(QChar(0xff1a))) + bone;
            }
    }
    return title;
}
void Canvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (property("mmdMobileLayout").toBool()) {
        p.fillRect(rect(), QColor(64, 64, 64));
        return;
    }
    if (fullScreen_) {
        p.fillRect(rect(), Qt::black);
        return;
    }
    const auto color = [](std::uint32_t value) {
        return QColor(value & 255, (value >> 8) & 255, (value >> 16) & 255);
    };
    const auto fill = [&](const ui::PanelFill& f) {
        const auto a = color(f.color), b = color(f.color2);
        QLinearGradient gradient(f.left, f.top, f.vertical ? f.left : f.right,
                                 f.vertical ? f.bottom : f.top);
        gradient.setColorAt(0, a);
        gradient.setColorAt(1, b);
        p.fillRect(QRect(f.left, f.top, f.right - f.left, f.bottom - f.top), gradient);
        p.setPen(Qt::black);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(f.left, f.top, f.right - f.left, f.bottom - f.top));
    };
    const auto& settings = session_.Document().envelope.settings;
    const int side = std::max(250, settings.editPanelWidth), bottom = height();
    const auto& o = settings.options;
    const ui::EditorLayoutOptions options{session_.Selected().model == 255,
                                          bool(o[1]),
                                          bool(o[2]),
                                          bool(o[3]),
                                          bool(o[4]),
                                          bool(o[5]),
                                          bool(o[6])};
    const auto viewport =
        viewportOnly_
            ? ui::MmdSeparateViewport(width(), height(), settings.renderWidth,
                                      settings.renderHeight)
            : ui::MmdViewport(width(), height(), side, settings.renderWidth, settings.renderHeight);
    if (viewportOnly_) {
        p.fillRect(rect(), Qt::black);
    } else {
        p.fillRect(rect(), color(ui::kDefaultThemeColors[0]));
        if (!detached_)
            p.fillRect(side + 9, 0, width() - side - 9, height() - 160, Qt::black);
        fill({0, bottom - 160, width(), bottom, ui::kDefaultThemeColors[0],
              ui::kDefaultThemeColors[1], true});
        for (const auto& f : ui::MmdPanelFills(side, bottom, options))
            fill(f);
        p.fillRect(side, 0, 6, bottom - 159, QColor(220, 220, 220));
        // The original TimelineDC contains two white waveform rows even
        // without an imported WAV. This is separate from the key grid.
        p.fillRect(6, 95, std::max(1, side - 10), 50, Qt::white);
        p.setPen(Qt::black);
        p.drawRect(6, 95, std::max(1, side - 11), 49);
        p.drawLine(6, 120, side - 5, 120);
    }
    if (viewportOnly_ || !detached_) {
        const int inset = viewportOnly_ ? 0 : side + 9;
        fill({inset, viewport.y - 25, width(), viewport.y - 1, 0x969696, 0x646464, true});
        p.drawImage(QPoint(width() - 58, viewport.y - 24), viewTools_);
        fill({inset, viewport.y + viewport.height, width(), viewport.y + viewport.height + 32,
              0x787878, 0xc8c8c8, true});
        const bool english = language_.EnglishLayout();
        const QString title = ViewportTitle();
        p.setPen(Qt::white);
        p.setFont(GlyphFont(language_.Code(), english ? 20 : 16));
        p.drawText(QRect(inset + 10, viewport.y - (english ? 22 : 20),
                         std::max(1, width() - inset - 20), 24),
                   Qt::AlignLeft | Qt::AlignTop, title);
        p.setPen(Qt::black);
        p.setFont(GlyphFont(language_.Code(), 14));
        const int stripY = viewport.y + viewport.height;
        const int positionX = options.cameraMode ? (english ? 90 : 72) : (english ? 66 : 68);
        const auto position = language_.Text(options.cameraMode ? "viewport.camera_position"
                                                                : "viewport.bone_position");
        if (language_.AdaptiveCaptions())
            p.setFont(FitCaptionFont(p.font(), position, 138 - positionX));
        p.drawText(QRect(inset + positionX, stripY + (english ? 10 : 11),
                         language_.AdaptiveCaptions() ? 138 - positionX : 100, 18),
                   Qt::AlignLeft | Qt::AlignTop, position);
        const auto angle = language_.Text("viewport.angle");
        p.setFont(language_.AdaptiveCaptions()
                      ? FitCaptionFont(GlyphFont(language_.Code(), 14), angle, english ? 33 : 29)
                      : GlyphFont(language_.Code(), 14));
        p.drawText(QRect(inset + (english ? 365 : 369), stripY + (english ? 10 : 11),
                         language_.AdaptiveCaptions() ? (english ? 33 : 29) : 65, 18),
                   Qt::AlignLeft | Qt::AlignTop, angle);
    }
    if (viewportOnly_)
        return;
    p.setPen(Qt::white);
    for (const auto& label : ui::MmdPanelLabels(side, bottom, options, language_.EnglishLayout())) {
        p.setFont(GlyphFont(language_.Code(), label.size));
        p.drawText(QRect(label.x, label.y, 250, label.size + 3), Qt::AlignTop | Qt::AlignLeft,
                   language_.Text(label.id));
    }
}

}
