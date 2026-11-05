#include "mobile_editor.hpp"
#include "timeline.hpp"
#include "interpolation.hpp"
#include "editor_font.hpp"
#include "mikudancestudio/mmd_editor_labels.hpp"
#include <QAbstractButton>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolBox>
#include <QVBoxLayout>
#include <QScroller>
#include <QTimer>

namespace mikudancestudio::qt {
namespace {
using namespace panel;
constexpr int kBottom = 640, kSide = 320;
constexpr double kPanelScale = 1.6;
class Section final : public QWidget {
public:
    Section(Language& language, QRect source, bool camera, bool timeline, QWidget* parent)
        : QWidget(parent), language_(language), source_(source), camera_(camera),
          timeline_(timeline) {
        setFixedSize(timeline ? source.size() : source.size() * kPanelScale);
    }
    QRect Source() const { return source_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(64, 64, 64));
        if (!timeline_)
            p.scale(kPanelScale, kPanelScale);
        p.translate(-source_.topLeft());
        ui::EditorLayoutOptions options;
        options.cameraMode = camera_;
        const auto color = [](unsigned c) {
            return QColor(c & 255, (c >> 8) & 255, (c >> 16) & 255);
        };
        for (const auto& f : ui::MmdPanelFills(kSide, kBottom, options)) {
            QLinearGradient gradient(f.left, f.top, f.vertical ? f.left : f.right,
                                     f.vertical ? f.bottom : f.top);
            gradient.setColorAt(0, color(f.color));
            gradient.setColorAt(1, color(f.color2));
            p.fillRect(QRect(f.left, f.top, f.right - f.left, f.bottom - f.top), gradient);
        }
        p.setPen(Qt::white);
        for (const auto& label :
             ui::MmdPanelLabels(kSide, kBottom, options, language_.EnglishLayout())) {
            p.setFont(GlyphFont(language_.Code(), label.size));
            p.drawText(QRect(label.x, label.y, 250, label.size + 3), Qt::AlignLeft | Qt::AlignTop,
                       language_.Text(label.id));
        }
    }

private:
    Language& language_;
    QRect source_;
    bool camera_, timeline_;
};
QScrollArea* Scroll(QWidget* content, QWidget* parent) {
    auto* area = new QScrollArea(parent);
    area->setWidget(content);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(false);
    QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
    return area;
}
bool Toggle(int id) {
    for (int value : {kExpandCameraPanelButton, kCollapseCameraPanelButton, kExpandLightPanelButton,
                      kCollapseLightPanelButton, kExpandBonePanelButton, kCollapseBonePanelButton,
                      kExpandMorphPanelButton, kCollapseMorphPanelButton,
                      kExpandAccessoryPanelButton, kCollapseAccessoryPanelButton,
                      kExpandSelfShadowPanelButton, kCollapseSelfShadowPanelButton})
        if (id == value)
            return true;
    return false;
}
bool Visible(int id, bool camera) {
    if (Toggle(id))
        return false;
    if ((id >= kPerspectiveCheckbox && id <= kCollapseCameraPanelButton) ||
        (id >= kLightColorSliderR && id <= kCollapseLightPanelButton) ||
        (id >= kAccessoryCombo && id <= kCollapseAccessoryPanelButton) ||
        (id >= kSelfShadowRangeSlider && id <= kCollapseSelfShadowPanelButton))
        return camera;
    if ((id >= kBoneSelectRadio && id <= kCollapseBonePanelButton) ||
        (id >= kMorphCombo0 && id <= kCollapseMorphPanelButton))
        return !camera;
    return true;
}

}
bool MobileEditor::Required(QSize size) {
    return size.width() < 1100 || size.height() < 560;
}
MobileEditor::MobileEditor(Language& language, QWidget* canvas)
    : QWidget(canvas), language_(language) {
    setObjectName("mobile.editor");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);
    auto* buttons = new QHBoxLayout;
    timelineButton_ = new QPushButton(this);
    toolsButton_ = new QPushButton(this);
    for (auto* button : {timelineButton_, toolsButton_}) {
        button->setCheckable(true);
        button->setMinimumHeight(36);
        buttons->addWidget(button);
    }
    timelineButton_->setObjectName("mobile.timeline");
    toolsButton_->setObjectName("mobile.controls");
    buttons->addStretch();
    root->addLayout(buttons);
    auto* body = new QHBoxLayout;
    body->setSpacing(2);
    view_ = new QWidget(this);
    viewLayout_ = new QVBoxLayout(view_);
    viewLayout_->setContentsMargins(0, 0, 0, 0);
    drawer_ = new QWidget(this);
    auto* drawerLayout = new QVBoxLayout(drawer_);
    drawerLayout->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(drawer_);
    timelinePage_ = new Section(language_, {0, 0, kSide, kBottom - 160}, true, true, this);
    auto* timelineScroll = Scroll(timelinePage_, pages_);
    // Ruler/key drags belong to the timeline. Its scroll bars remain available;
    // kinetic scrolling here would steal the editing gesture from that child.
    QScroller::ungrabGesture(timelineScroll->viewport());
    pages_->addWidget(timelineScroll);
    tools_ = new QToolBox(pages_);
    tools_->setStyleSheet("QToolBox::tab { min-height: 32px; }");
    auto* toolScroll = Scroll(tools_, pages_);
    toolScroll->setWidgetResizable(true);
    connect(tools_, &QToolBox::currentChanged, this, [this, toolScroll] {
        // Changing the active page moves all following tabs. Retain access
        // to the selected panel instead of leaving the old scroll offset.
        QTimer::singleShot(0, this, [this, toolScroll] {
            if (auto* current = tools_->currentWidget())
                toolScroll->ensureWidgetVisible(current, 0, 0);
        });
    });
    pages_->addWidget(toolScroll);
    drawerLayout->addWidget(pages_);
    body->addWidget(view_, 1);
    body->addWidget(drawer_);
    root->addLayout(body, 1);
    connect(timelineButton_, &QPushButton::clicked, this, [this] { SelectPage(0); });
    connect(toolsButton_, &QPushButton::clicked, this, [this] { SelectPage(1); });
    drawer_->hide();
}
void MobileEditor::SelectPage(int index) {
    const bool open = drawer_->isHidden() || pages_->currentIndex() != index;
    pages_->setCurrentIndex(index);
    drawer_->setVisible(open);
    timelineButton_->setChecked(open && index == 0);
    toolsButton_->setChecked(open && index == 1);
}
void MobileEditor::BuildSections(bool camera) {
    // Take the actual panel extents, including the different bone/morph widths.
    // All original horizontal-collapse buttons are replaced by toolbox tabs.
    ui::EditorLayoutOptions options;
    options.cameraMode = camera;
    for (const auto& f : ui::MmdPanelFills(kSide, kBottom, options)) {
        if (f.top != kBottom - 157)
            continue;
        auto* section =
            new Section(language_, {f.left - 3, kBottom - 160, f.right - f.left + 6, 160}, camera,
                        false, tools_);
        sections_.push_back(section);
        tools_->addItem(Scroll(section, tools_), QString());
    }
    // Separate readouts retain normal QLineEdit/IME behavior and never steal
    // keyboard focus or copy the session into a second set of controls.
    readouts_ = new QWidget(tools_);
    readouts_->setMinimumWidth(280);
    tools_->addItem(Scroll(readouts_, tools_), QString());
    // Reserve the active panel's height as well as every tab. Otherwise a
    // landscape phone can allocate almost zero height to the selected panel;
    // its clipped controls then sit beneath another tab's touch target.
    tools_->setMinimumHeight(tools_->count() * 36 + qRound(160 * kPanelScale));
    cameraMode_ = camera;
}
void MobileEditor::Arrange(const std::map<int, QWidget*>& controls, Timeline* timeline,
                           Interpolation* interpolation, QWidget* viewport, bool camera) {
    if (!arranged_ || cameraMode_ != camera) {
        // Reparent live widgets before deleting an old section and its layout.
        for (const auto& [id, widget] : controls)
            widget->setParent(parentWidget());
        interpolation->setParent(parentWidget());
        while (tools_->count()) {
            auto* page = tools_->widget(0);
            tools_->removeItem(0);
            delete page;
        }
        sections_.clear();
        BuildSections(camera);
    }
    arranged_ = true;
    setGeometry(parentWidget()->rect());
    drawer_->setFixedWidth(std::min(340, std::max(280, width() / 3)));
    timelineButton_->setText(language_.Text("mobile.timeline"));
    toolsButton_->setText(language_.Text("mobile.controls"));
    if (viewport->parentWidget() != view_) {
        viewport->setParent(view_);
        viewLayout_->addWidget(viewport);
    }
    viewport->show();
    if (timeline->parentWidget() != timelinePage_)
        timeline->setParent(timelinePage_);
    timeline->setGeometry(6, 145, kSide - 25, kBottom - 393);
    timeline->show();
    ui::EditorLayoutOptions options;
    options.cameraMode = camera;
    auto* readoutForm = qobject_cast<QFormLayout*>(readouts_->layout());
    if (!readoutForm)
        readoutForm = new QFormLayout(readouts_);
    for (const auto& rect : ui::MmdEditorControls(kSide, kBottom, options)) {
        auto it = controls.find(rect.id);
        if (it == controls.end())
            continue;
        auto* widget = it->second;
        bool visible = Visible(rect.id, camera);
        QWidget* destination = timelinePage_;
        QPoint origin;
        double scale = 1;
        if (rect.y >= kBottom - 160) {
            visible = false;
            for (auto* sectionWidget : sections_) {
                auto* section = static_cast<Section*>(sectionWidget);
                if (section->Source().contains(QPoint(rect.x, rect.y))) {
                    destination = section;
                    origin = section->Source().topLeft();
                    scale = kPanelScale;
                    visible = Visible(rect.id, camera);
                    break;
                }
            }
        }
        if (widget->parentWidget() != destination)
            widget->setParent(destination);
        widget->setGeometry(
            qRound((rect.x - origin.x()) * scale), qRound((rect.y - origin.y()) * scale),
            qRound(rect.width * scale),
            qRound((qobject_cast<QComboBox*>(widget) ? 20 : std::max(1, rect.height)) * scale));
        widget->setVisible(visible);
    }
    auto* firstSection = static_cast<Section*>(sections_.front());
    if (interpolation->parentWidget() != firstSection)
        interpolation->setParent(firstSection);
    interpolation->move(8 * kPanelScale, 25 * kPanelScale);
    interpolation->SetDisplayScale(kPanelScale);
    interpolation->show();
    int index = 0;
    for (auto* section : sections_) {
        QString caption;
        const auto source = static_cast<Section*>(section)->Source();
        for (const auto& label :
             ui::MmdPanelLabels(kSide, kBottom, options, language_.EnglishLayout()))
            if (label.y <= kBottom - 145 && source.contains(QPoint(label.x, label.y))) {
                caption = language_.Text(label.id);
                break;
            }
        tools_->setItemText(index++, caption);
        section->update();
    }
    tools_->setItemText(index, language_.Text("mobile.viewport"));
    // The desktop viewport header/readout widgets are placed in a scrollable
    // form so no fixed 740-pixel strip can overflow a phone viewport.
    const auto v = ui::MmdSeparateViewport(900, 600, 640, 480);
    for (const auto& rect : ui::MmdViewportControls(-9, 900, v)) {
        auto it = controls.find(rect.id);
        if (it == controls.end())
            continue;
        auto* widget = it->second;
        if (widget->parentWidget() != readouts_) {
            widget->setParent(readouts_);
            QString label;
            const int positions[]{kReadoutPosXEdit, kReadoutPosYEdit, kReadoutPosZEdit};
            const int rotations[]{kReadoutRotXEdit, kReadoutRotYEdit, kReadoutRotZEdit};
            for (int axis = 0; axis < 3; ++axis) {
                if (rect.id == positions[axis])
                    label = QStringLiteral("XYZ").mid(axis, 1);
                if (rect.id == rotations[axis])
                    label = QStringLiteral("R") + QStringLiteral("xyz").mid(axis, 1);
            }
            if (rect.id == kReadoutDistEdit)
                label = QStringLiteral("dist");
            widget->setMinimumHeight(32);
            readoutForm->addRow(label, widget);
        }
        widget->show();
    }
    readouts_->adjustSize();
    timelinePage_->update();
    if (isHidden()) {
        show();
        raise();
    }
    // Raising a QWidget containing a native QWindow also raises that child.
    // Qt Android then activates the Vulkan window, even above a modal dialog.
    // In particular an IME resize must only relayout: stealing the file editor's
    // focus here immediately closes its input connection and keyboard again.
}
void MobileEditor::Restore(const std::map<int, QWidget*>& controls, Timeline* timeline,
                           Interpolation* interpolation, QWidget* viewport) {
    auto* canvas = parentWidget();
    for (const auto& [id, widget] : controls) {
        widget->setParent(canvas);
        widget->setMinimumHeight(0);
    }
    timeline->setParent(canvas);
    interpolation->setParent(canvas);
    interpolation->SetDisplayScale(1);
    viewport->setParent(canvas);
    viewport->show();
    arranged_ = false;
    hide();
}
}
