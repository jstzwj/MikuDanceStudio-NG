#include "editor_window.hpp"
#include "editor_canvas.hpp"
#include "model_physics_dialog.hpp"
#include "mikudancestudio/sidebar_navigation.hpp"
#include <QComboBox>
#include <QMenuBar>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollBar>

namespace mikudancestudio::qt {
using namespace editor;
using namespace panel;
void EditorWindow::CreateViewport(const rhi::BackendOptions& options) {
    // A different API receives a fresh native window. CPU state remains owned
    // by EditorSession; retiring the old controller never evaluates a frame.
    auto* next = new Viewport(session_, assets_);
    next->SetOptions(options);
    auto* container = QWidget::createWindowContainer(next, separate_ ? separate_ : canvas_);
    auto* old = viewportContainer_;
    viewport_ = next;
    viewport_->installEventFilter(this);
    viewportContainer_ = container;
    if (old)
        delete old;
    viewport_->changed = [this](bool ok, const std::string& reason) {
        Result(ok, reason);
        Refresh();
    };
    viewport_->physicsEdit = [this] {
        render::PhysicsEditView view;
        if (physicsDialog_ && physicsDialog_->Active()) {
            view.edit = &physicsDialog_->State();
            view.slot = session_.Selected().model;
            view.body = physicsDialog_->SelectedBody();
            view.joint = physicsDialog_->SelectedJoint();
            view.jointPage = physicsDialog_->JointPage();
        }
        return view;
    };
    viewport_->physicsDrag = [this](unsigned mode, int dy, bool shift) {
        return physicsDialog_ && physicsDialog_->Drag(mode, dy, shift);
    };
    viewport_->zoom = [this](int delta) {
        std::string r;
        Result(session_.WheelView(delta, r), r);
        Refresh();
    };
    viewport_->drag = [this](QPoint delta, Qt::MouseButtons buttons,
                             Qt::KeyboardModifiers modifiers) {
        if (!(buttons & Qt::RightButton) && !(buttons & Qt::MiddleButton))
            return;
        std::string r;
        bool ok = true;
        if (buttons & Qt::RightButton)
            ok = session_.DragView(false, delta.x(), delta.y(), modifiers & Qt::ShiftModifier,
                                   modifiers & Qt::ControlModifier, r);
        if (ok && (buttons & Qt::MiddleButton))
            ok = session_.DragView(true, delta.x(), delta.y(), modifiers & Qt::ShiftModifier,
                                   modifiers & Qt::ControlModifier, r);
        Result(ok, r);
        Refresh();
    };
    Layout();
    // A replacement child of an already-visible QWidget is not implicitly
    // shown. Ensure Qt attaches/maps the new native child on every platform.
    // A hidden ancestor still keeps hidden-window acceptance off the desktop.
    container->show();
}
void EditorWindow::SetSeparateWindow(bool enabled) {
    if (enabled == (separate_ != nullptr))
        return;
    QScopedValueRollback transition(windowTransition_, true);
    FinishSidebarDrag();
    viewport_->CancelInput();
    auto options = viewport_->Options();
    options.surface.reset();
    QWidget* retired = nullptr;
    if (enabled) {
        dockedSidebarWidth_ = session_.Document().envelope.settings.editPanelWidth;
        auto* window = new Canvas(language_, session_, assets_, nullptr, true);
        window->setObjectName("mmd.separateViewport");
        window->setAcceptDrops(true);
        window->setWindowTitle("MMD");
        window->setFont(font());
        window->setWindowFlag(Qt::WindowStaysOnTopHint, separateOnTop_);
        window->resize(separateSize_);
        if (!separateGeometry_.isEmpty())
            window->restoreGeometry(separateGeometry_);
        // Qt clamps restored geometry to the current screen. MMD preserves the
        // user's normal size, even when it exceeds the available desktop area.
        if (!window->isMaximized())
            window->resize(separateSize_);
        separate_ = window;
        window->installEventFilter(this);
    } else {
        retired = separate_;
        separateGeometry_ = retired->saveGeometry();
        if (!retired->isMaximized())
            separateSize_ = retired->size();
        retired->removeEventFilter(this);
        separate_ = nullptr;
        session_.SetSidebarWidth(dockedSidebarWidth_);
        sidebarClientWidth_ = 0;
    }
    for (int id = kModelEditToggle; id <= kCoordAxisCheckbox; ++id)
        Control(id)->setParent(separate_ ? separate_ : canvas_);
    static_cast<Canvas*>(canvas_)->SetDetached(enabled);
    CreateViewport(options);
    BuildMenus();
    Refresh();
    if (separate_ && !testing_)
        separate_->show();
    if (retired) {
        retired->hide();
        retired->deleteLater(); // Closing the separate window never closes the project.
    }
}
void EditorWindow::ToggleSeparateOnTop() {
    QScopedValueRollback transition(windowTransition_, true);
    separateOnTop_ = !separateOnTop_;
    if (separate_) {
        const bool visible = separate_->isVisible();
        // Changing QWidget flags in place invalidates X11 native ancestry even
        // when the embedded QWindow is replaced. Retire the complete parent
        // hierarchy through the same ownership path as ordinary detach/dock.
        SetSeparateWindow(false);
        SetSeparateWindow(true);
        if (visible)
            separate_->show();
    }
    RefreshMenus();
}
void EditorWindow::SetFullScreenViewport(bool enabled) {
    if (enabled == fullScreen_)
        return;
    QScopedValueRollback transition(windowTransition_, true);
    FinishSidebarDrag();
    viewport_->CancelInput();
    auto options = viewport_->Options();
    options.surface.reset();
    viewport_->SetOptions(options);
    if (enabled) {
        restoreSeparate_ = separate_ != nullptr;
        if (separate_)
            SetSeparateWindow(false);
        normalGeometry_ = saveGeometry();
        normalSize_ = size();
        normalWindowState_ = windowState();
        normalSidebarWidth_ = session_.Document().envelope.settings.editPanelWidth;
        fullScreen_ = true;
        static_cast<Canvas*>(canvas_)->SetFullScreen(true);
        menuBar()->hide();
        // setWindowState preserves visibility. Hidden acceptance never calls
        // showFullScreen or changes a display mode.
        setWindowState(normalWindowState_ | Qt::WindowFullScreen);
    } else {
        // Keep the fullscreen layout active through native resize notifications,
        // so they cannot alter the saved sidebar ratio on the way back.
        setWindowState(normalWindowState_);
        restoreGeometry(normalGeometry_);
        if (!(normalWindowState_ & Qt::WindowMaximized))
            resize(normalSize_);
        fullScreen_ = false;
        static_cast<Canvas*>(canvas_)->SetFullScreen(false);
        menuBar()->show();
        session_.SetSidebarWidth(normalSidebarWidth_);
        sidebarClientWidth_ = 0;
    }
    // Retire device/surface bindings; a later Render reacquires the current
    // native surface without reloading or advancing the CPU scene.
    viewport_->SetOptions(options);
    if (!enabled && restoreSeparate_) {
        restoreSeparate_ = false;
        SetSeparateWindow(true);
    }
    Refresh();
}
void EditorWindow::Layout() {
    if (!canvas_)
        return;
    if (fullScreen_) {
        const auto& settings = session_.Document().envelope.settings;
        const auto rect =
            ui::MmdSeparateViewport(canvas_->width(), canvas_->height(), int(settings.renderWidth),
                                    int(settings.renderHeight), true);
        viewportContainer_->setGeometry(rect.x, rect.y, std::max(1, rect.width),
                                        std::max(1, rect.height));
        viewport_->resize(std::max(1, rect.width), std::max(1, rect.height));
        for (const auto& pair : controls_)
            pair.second->hide();
        timeline_->hide();
        interpolation_->hide();
        canvas_->update();
        return;
    }
    timeline_->show();
    interpolation_->show();
    side_ = std::max(250, session_.Document().envelope.settings.editPanelWidth);
    const int clientWidth = canvas_->width();
    if (separate_) {
        side_ = std::max(250, clientWidth - 3);
        session_.SetSidebarWidth(side_);
    } else if (clientWidth >= 255) {
        if (!sidebarClientWidth_ || side_ != sidebarStoredWidth_)
            sidebarRatio_ = float(double(side_) / double(clientWidth));
        else if (clientWidth != sidebarClientWidth_) {
            side_ = ui::ResizeSidebar(sidebarRatio_, clientWidth);
            session_.SetSidebarWidth(side_);
        }
        sidebarClientWidth_ = clientWidth;
        sidebarStoredWidth_ = side_;
    }
    layout_.cameraMode = session_.Selected().model == 255;
    const auto& options = session_.Document().envelope.settings.options;
    layout_.cameraPanelExpanded = options[1];
    layout_.lightPanelExpanded = options[2];
    layout_.accessoryPanelExpanded = options[3];
    layout_.bonePanelExpanded = options[4];
    layout_.morphPanelExpanded = options[5];
    layout_.selfShadowPanelExpanded = options[6];
    const auto viewport =
        separate_ ? ui::MmdSeparateViewport(separate_->width(), separate_->height(),
                                            int(session_.Document().envelope.settings.renderWidth),
                                            int(session_.Document().envelope.settings.renderHeight))
                  : ui::MmdViewport(canvas_->width(), canvas_->height(), side_,
                                    int(session_.Document().envelope.settings.renderWidth),
                                    int(session_.Document().envelope.settings.renderHeight));
    if (viewportContainer_) {
        viewportContainer_->setGeometry(viewport.x, viewport.y, std::max(1, viewport.width),
                                        std::max(1, viewport.height));
        // A hidden QWidget container postpones its child QWindow resize until
        // exposure. Input/projection still need the same dimensions beforehand.
        viewport_->resize(std::max(1, viewport.width), std::max(1, viewport.height));
    }
    const auto place = [&](const auto& rect) {
        auto* w = Control(rect.id);
        if (!w)
            return;
        w->setGeometry(rect.x, rect.y, std::max(1, rect.width),
                       qobject_cast<QComboBox*>(w) ? 20 : std::max(1, rect.height));
        w->raise();
    };
    for (const auto& rect : ui::MmdEditorControls(side_, canvas_->height(), layout_))
        place(rect);
    for (const auto& rect : ui::MmdViewportControls(
             separate_ ? -9 : side_, separate_ ? separate_->width() : canvas_->width(), viewport))
        place(rect);
    timeline_->setGeometry(6, 145, std::max(1, side_ - 25), std::max(1, canvas_->height() - 393));
    static_cast<QScrollBar*>(Control(kTimelineHScroll))
        ->setPageStep(std::max(1, (side_ - 100) / 13));
    interpolation_->move(8, canvas_->height() - 135);
    for (const auto& pair : controls_) {
        const auto id = pair.first;
        bool show = true;
        const struct {
            int first, collapse, expand, option;
            bool camera;
        } groups[]{
            {kPerspectiveCheckbox, kCollapseCameraPanelButton, kExpandCameraPanelButton, 1, true},
            {kLightColorSliderR, kCollapseLightPanelButton, kExpandLightPanelButton, 2, true},
            {kAccessoryCombo, kCollapseAccessoryPanelButton, kExpandAccessoryPanelButton, 3, true},
            {kBoneSelectRadio, kCollapseBonePanelButton, kExpandBonePanelButton, 4, false},
            {kMorphCombo0, kCollapseMorphPanelButton, kExpandMorphPanelButton, 5, false},
            {kSelfShadowRangeSlider, kCollapseSelfShadowPanelButton, kExpandSelfShadowPanelButton,
             6, true}};
        for (const auto& group : groups) {
            if (id >= group.first && id <= group.collapse)
                show = layout_.cameraMode == group.camera && options[group.option];
            if (id == group.expand)
                show = layout_.cameraMode == group.camera && !options[group.option];
        }
        pair.second->setVisible(show);
    }
    canvas_->update();
    if (separate_)
        separate_->update();
}
void EditorWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    Layout();
}

}
