#include "viewport.hpp"
#include "mikudancestudio/background_placement.hpp"
#include <QPlatformSurfaceEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QCursor>
#include <cmath>
#include <chrono>
namespace mikudancestudio::qt {
namespace {
render::SceneRenderSources Sources(const editor::EditorAssets& assets) {
    render::SceneRenderSources out;
    out.models = assets.modelPaths;
    out.accessories = assets.accessoryPaths;
    out.picture = assets.picture;
    return out;
}
}
Viewport::Viewport(editor::EditorSession& session, const std::filesystem::path& assets)
    : session_(session), builtins_(assets) {
    setObjectName("mmd.viewport");
    setSurfaceType(QSurface::RasterSurface);
    options_.backend = rhi::DefaultBackend();
    options_.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options_.driver = rhi::BackendDriver::Software;
}
Viewport::~Viewport() {
    changed = {};
    FinishToolDrag();
    FinishBoxSelection();
    controller_.Close();
}
void Viewport::SetOptions(const rhi::BackendOptions& options) {
    FinishToolDrag();
    FinishBoxSelection();
    frameCached_ = false;
    controller_.Close();
    producer_.ResetHistory();
    options_ = options;
}
bool Viewport::Reset(std::string& reason) {
    FinishToolDrag();
    FinishBoxSelection();
    frameCached_ = false;
    producer_.ResetHistory();
    if (!controller_.ReplaceDevice()) {
        reason = controller_.Reason();
        return false;
    }
    return true;
}
bool Viewport::OutputLimits(QSize& output, std::string& reason) const {
    // Reuse the active device where possible. Before first presentation, query
    // a tiny offscreen device without creating a surface or touching the desktop.
    auto* device = controller_.GetDevice();
    rhi::OffscreenBackendDevice probe;
    if (!device) {
        auto options = options_;
        options.surface.reset();
        probe = rhi::CreateOffscreenBackendDevice(1, 1, options);
        if (!probe) {
            reason = probe.reason;
            return false;
        }
        device = probe.device.get();
    }
    const auto limits = device->TargetLimits();
    if (!limits.width || !limits.height || limits.width > 0x7fffffffu ||
        limits.height > 0x7fffffffu) {
        reason = "render-target-limits-unavailable";
        return false;
    }
    // The portable offscreen factory and sequence exporter currently bound
    // each dimension to 4096, independently of a larger native GPU limit.
    output = QSize(int(std::min(4096u, limits.width)), int(std::min(4096u, limits.height)));
    reason.clear();
    return true;
}
bool Viewport::Render(std::string& reason, bool hiddenAcceptance) {
    if ((!isExposed() && !hiddenAcceptance) || width() < 1 || height() < 1)
        return true;
    const auto w = unsigned(std::max(1, qRound(width() * devicePixelRatio())));
    const auto h = unsigned(std::max(1, qRound(height() * devicePixelRatio())));
    if (controller_.State() == rhi::PreviewState::Closed) {
        auto surface = QtSurface(*this, reason);
        if (!surface)
            return false;
        rhi::SwapchainDesc desc;
        desc.window = surface->window;
        desc.width = w;
        desc.height = h;
        desc.depth = rhi::SurfaceDepth::Depth24Stencil8;
        options_.surface = std::move(surface);
        if (!controller_.Open(desc, options_)) {
            reason = controller_.Reason();
            return false;
        }
    }
    if (!controller_.Resize(w, h)) {
        reason = controller_.Reason();
        return false;
    }
    if (!PrepareFrame(w, h, reason))
        return false;
    const auto submissions = Submissions();
    if (!controller_.Draw(cachedFrame_)) {
        frameCached_ = false;
        reason = controller_.Reason();
        return false;
    }
    if (Submissions() > submissions && producer_.Commit())
        frameCached_ = false;
    reason.clear();
    return true;
}
bool Viewport::PrepareFrame(unsigned w, unsigned h, std::string& reason) {
    using Clock = std::chrono::steady_clock;
    const auto now = [&] { return profileFrame_ ? Clock::now() : Clock::time_point{}; };
    auto start = now();
    const auto elapsed = [&] {
        const auto end = now();
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        start = end;
        return ms;
    };
    cpuTimings_ = {};
    if (!w || !h || w > 4096 || h > 4096) {
        reason = "scene_render_settings";
        return false;
    }
    if (!session_.PrepareView(reason) || !overlay_.Load(builtins_, reason))
        return false;
    cpuTimings_.view = elapsed();
    const auto& assets = session_.Assets();
    if (!loadedAssets_ || assets.models != loaded_.models ||
        assets.accessories != loaded_.accessories || assets.modelPaths != loaded_.modelPaths ||
        assets.accessoryPaths != loaded_.accessoryPaths || assets.picture != loaded_.picture) {
        if (!producer_.Load(session_.Runtime(), Sources(assets), builtins_, reason))
            return false;
        loaded_ = assets;
        loadedAssets_ = true;
        frameCached_ = false;
    }
    cpuTimings_.assets = elapsed();
    render::SceneRenderSettings settings;
    settings.width = w;
    settings.height = h;
    settings.editorModel = session_.Selected().model;
    const auto revision = session_.Revision(), updates = session_.Runtime().Counters().frames;
    const bool history = session_.Document().view.captureMode != 0;
    if (!frameCached_ || history || cachedRevision_ != revision || cachedUpdates_ != updates ||
        cachedWidth_ != w || cachedHeight_ != h || cachedModel_ != session_.Selected().model ||
        cachedBone_ != ActiveBone() || cachedPlaying_ != session_.Playing() ||
        cachedSelectedBones_ != session_.Selected().bones ||
        cachedEditMode_ != session_.Document().timeline.editMode ||
        cachedCoordinateSystem_ != session_.CoordinateSystem() ||
        cachedToolOperation_ != toolOperation_) {
        frameCached_ = false;
        if (!producer_.Build(session_.Runtime(), settings, cachedFrame_, reason))
            return false;
        cpuTimings_.scene = elapsed();
        render::ViewportTools tools;
        tools.coordinateSystem = int(session_.CoordinateSystem());
        tools.operation = toolOperation_;
        tools.dragMode = int(session_.BoneDragMode());
        tools.selectionBox = box_;
        const auto start = DevicePoint(anchor_), end = DevicePoint(previous_);
        tools.box = {start.x(), start.y(), end.x(), end.y()};
        if (!overlay_.Append(session_.Runtime(), settings, session_.Selected().bones,
                             session_.Playing(), cachedFrame_, reason, &tools))
            return false;
        cpuTimings_.overlay = elapsed();
        ++frameBuilds_;
        cachedRevision_ = revision;
        cachedUpdates_ = updates;
        cachedWidth_ = w;
        cachedHeight_ = h;
        cachedModel_ = session_.Selected().model;
        cachedBone_ = ActiveBone();
        cachedSelectedBones_ = session_.Selected().bones;
        cachedEditMode_ = session_.Document().timeline.editMode;
        cachedCoordinateSystem_ = session_.CoordinateSystem();
        cachedToolOperation_ = toolOperation_;
        cachedPlaying_ = session_.Playing();
        frameCached_ = !history;
    }
    reason.clear();
    return true;
}
int Viewport::ActiveBone() const {
    return session_.ActiveBone();
}
int Viewport::ToolAt(QPoint point) {
    render::ViewportTools tools;
    tools.camera = session_.Selected().model == 255;
    tools.playing = session_.Playing();
    tools.editMode = session_.Document().timeline.editMode;
    const auto ratio = devicePixelRatio();
    const auto w = unsigned(std::max(1, qRound(width() * ratio)));
    const auto h = unsigned(std::max(1, qRound(height() * ratio)));
    if (!tools.camera && ActiveBone() >= 0) {
        std::string reason;
        if (!session_.PrepareView(reason) ||
            !render::SceneBoneScreenPoints(session_.Runtime(), session_.Selected().model, w, h,
                                           bonePoints_, reason, session_.Selected().model))
            return 0;
        if (unsigned(ActiveBone()) < bonePoints_.size()) {
            tools.centerX = bonePoints_[unsigned(ActiveBone())].x;
            tools.centerY = bonePoints_[unsigned(ActiveBone())].y;
        }
    }
    return render::ViewportToolAt(tools, qRound(point.x() * ratio), qRound(point.y() * ratio),
                                  float(w), float(h), render::BackgroundViewScale(int(w)));
}
std::uint64_t Viewport::Submissions() const {
    const auto* renderer = const_cast<rhi::ScenePreviewController&>(controller_).GetRenderer();
    return renderer ? renderer->Stats().submissions : 0;
}
QPoint Viewport::DevicePoint(QPoint point) const {
    return {qRound(point.x() * devicePixelRatio()), qRound(point.y() * devicePixelRatio())};
}
void Viewport::FinishBoxSelection() {
    if (!box_)
        return;
    std::string reason;
    bool ok = true;
    if (!session_.Playing() && session_.Selected().model == boxModel_) {
        const auto bounds = QRect(DevicePoint(anchor_), DevicePoint(previous_)).normalized();
        ok = session_.BoxSelectBones(bonePoints_, bounds.left(), bounds.top(), bounds.right(),
                                     bounds.bottom(), boxAdditive_, true, reason);
    }
    box_ = false;
    pressed_ = false;
    frameCached_ = false;
    if (isExposed())
        setMouseGrabEnabled(false);
    if (changed)
        changed(ok, reason);
}
bool Viewport::BeginCenteredCursor(int operation, QWindow* inputSurface) {
    if (operation < 9 && operation != 1 && operation != 2)
        return true;
    const auto ratio = devicePixelRatio();
    const int w = qRound(width() * ratio), h = qRound(height() * ratio);
    if (operation >= 9) {
        const auto restore =
            render::ViewportToolRestorePoint(operation, w, h, render::BackgroundViewScale(w));
        restoreCursor_ = {qRound(restore[0] / ratio), qRound(restore[1] / ratio)};
    }
    savedCursor_ = cursor();
    centeredCursor_ = true;
    setCursor(Qt::BlankCursor);
    // Hidden/offscreen tests never request capture or global pointer movement.
    cursorWarped_ = isExposed() && !QGuiApplication::platformName().startsWith("wayland");
    if (cursorWarped_) {
        previous_ = {qRound((w / 2) / ratio), qRound((h / 2) / ratio)};
        QCursor::setPos(mapToGlobal(previous_));
    }
    toolPrevious_ = DevicePoint(previous_);
    relativePosition_ = toolPrevious_;
    if (isExposed() && QGuiApplication::platformName().startsWith("wayland")) {
        if (!relativePointer_)
            relativePointer_ = CreateWaylandRelativePointer();
        auto* surface = inputSurface ? inputSurface : this;
        std::string reason;
        // The pointer is focused on the canvas for a header press, and on the
        // child native window for an axis press. Lock the actual focused surface.
        const auto restore = surface->mapFromGlobal(mapToGlobal(restoreCursor_));
        relativePosition_ = QPoint(w / 2, h / 2);
        toolPrevious_ = relativePosition_.toPoint();
        const bool ok =
            relativePointer_ &&
            relativePointer_->Begin(
                *surface, restore,
                [this](QPointF delta) {
                    relativePosition_ += delta * devicePixelRatio();
                    if (!std::isfinite(relativePosition_.x()) ||
                        !std::isfinite(relativePosition_.y()) ||
                        std::abs(relativePosition_.x()) > 100000000 ||
                        std::abs(relativePosition_.y()) > 100000000) {
                        CancelInput();
                        if (changed)
                            changed(false, "relative-pointer-position-overflow");
                        return;
                    }
                    ToolMotion(relativePosition_.toPoint(), QGuiApplication::keyboardModifiers());
                },
                [this](const std::string& error) {
                    CancelInput();
                    if (changed)
                        changed(false, error);
                },
                reason);
        if (!ok) {
            FinishToolDrag();
            if (changed)
                changed(false, reason.empty() ? "wayland-relative-pointer-unavailable" : reason);
            return false;
        }
    }
    return true;
}
void Viewport::FinishToolDrag(bool restorePosition) {
    if (!boneDragMode_ && !sceneDragMode_ && !headerDragOperation_)
        return;
    session_.EndBoneDrag();
    boneDragMode_ = 0;
    sceneDragMode_ = 0;
    headerDragOperation_ = 0;
    dragResource_.reset();
    pressed_ = false;
    toolOperation_ = 0;
    if (centeredCursor_) {
        if (relativePointer_)
            relativePointer_->End(restorePosition);
        const bool warp = cursorWarped_ && restorePosition && isExposed();
        centeredCursor_ = cursorWarped_ = false;
        setCursor(savedCursor_);
        if (warp)
            QCursor::setPos(mapToGlobal(restoreCursor_));
    }
    if (isExposed() && !QGuiApplication::platformName().startsWith("wayland"))
        setMouseGrabEnabled(false);
}
bool Viewport::BeginHeaderDrag(unsigned operation, QPoint position, QPoint restore,
                               QWindow* inputSurface) {
    if ((operation != 1 && operation != 2) || boneDragMode_ || sceneDragMode_ ||
        headerDragOperation_ || box_)
        return false;
    headerDragOperation_ = operation;
    pressed_ = true;
    previous_ = position;
    restoreCursor_ = restore;
    if (!BeginCenteredCursor(int(operation), inputSurface))
        return false;
    if (isExposed() && !QGuiApplication::platformName().startsWith("wayland"))
        setMouseGrabEnabled(true);
    if (changed)
        changed(true, {});
    return true;
}
void Viewport::CancelInput() {
    const bool active = boneDragMode_ || sceneDragMode_ || headerDragOperation_ || box_;
    FinishToolDrag();
    FinishBoxSelection();
    pressed_ = false;
    if (active && changed)
        changed(true, {});
}
bool Viewport::event(QEvent* event) {
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
        event->type() == QEvent::UngrabMouse) {
        CancelInput();
    }
    if (event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        FinishToolDrag();
        FinishBoxSelection();
        frameCached_ = false;
        controller_.Close();
        producer_.ResetHistory();
        options_.surface.reset();
    }
    return QWindow::event(event);
}
void Viewport::mousePressEvent(QMouseEvent* e) {
    if (boneDragMode_ || sceneDragMode_ || headerDragOperation_)
        return;
    pressed_ = true;
    previous_ = e->position().toPoint();
    const int operation = e->button() == Qt::LeftButton ? ToolAt(previous_) : 0;
    if (operation == 21) {
        session_.CycleCoordinateSystem();
        if (changed)
            changed(true, {});
        return;
    }
    if (operation >= 15 && operation <= 20 && session_.Selected().model == 255 &&
        !session_.Playing()) {
        sceneDragMode_ = unsigned(operation - 2);
        toolOperation_ = operation;
        if (!BeginCenteredCursor(operation))
            return;
        if (isExposed() && !QGuiApplication::platformName().startsWith("wayland"))
            setMouseGrabEnabled(true);
        if (changed)
            changed(true, {});
        return;
    }
    if (e->button() == Qt::LeftButton && session_.Selected().model != 255 && !session_.Playing()) {
        std::string reason;
        if (!session_.PrepareView(reason)) {
            if (changed)
                changed(false, reason);
            return;
        }
        if (!render::SceneBoneScreenPoints(
                session_.Runtime(), session_.Selected().model,
                unsigned(std::max(1, qRound(width() * devicePixelRatio()))),
                unsigned(std::max(1, qRound(height() * devicePixelRatio()))), bonePoints_, reason,
                session_.Selected().model)) {
            if (changed)
                changed(false, reason);
            return;
        }
        if (operation >= 3 && operation <= 14 && ActiveBone() >= 0 &&
            unsigned(ActiveBone()) < bonePoints_.size()) {
            static constexpr int modes[]{1, 2, 3, 7, 8, 9, 4, 5, 6, 10, 11, 12};
            const auto ratio = devicePixelRatio();
            boneDrag_ = {};
            boneDrag_.mode = modes[operation - 3];
            boneDrag_.viewWidth = std::max(1, qRound(width() * ratio));
            boneDrag_.viewHeight = std::max(1, qRound(height() * ratio));
            // Use the physical viewport for picking, clip W and drag sensitivity together.
            if (!render::SceneBoneScreenPoints(session_.Runtime(), session_.Selected().model,
                                               unsigned(boneDrag_.viewWidth),
                                               unsigned(boneDrag_.viewHeight), bonePoints_, reason,
                                               session_.Selected().model)) {
                if (changed)
                    changed(false, reason);
                return;
            }
            const auto& active = bonePoints_[unsigned(ActiveBone())];
            boneDrag_.originX = active.x;
            boneDrag_.originY = active.y;
            boneDrag_.clipW = active.clipW;
            boneDrag_.viewScale = render::BackgroundViewScale(boneDrag_.viewWidth);
            if (boneDrag_.mode >= 7 && boneDrag_.mode <= 9 && boneDrag_.clipW <= 0) {
                if (changed)
                    changed(false, "bone-drag-projection-unavailable");
                return;
            }
            if (!session_.BeginBoneDrag(unsigned(boneDrag_.mode), reason)) {
                if (changed)
                    changed(false, reason);
                return;
            }
            boneDragMode_ = boneDrag_.mode;
            dragModel_ = session_.Selected().model;
            dragResource_ = session_.Runtime().Resource(dragModel_);
            toolOperation_ = operation;
            toolPrevious_ = DevicePoint(previous_);
            if (!BeginCenteredCursor(operation))
                return;
            if (isExposed() && !QGuiApplication::platformName().startsWith("wayland"))
                setMouseGrabEnabled(true);
            if (changed)
                changed(true, {});
            return;
        }
        anchor_ = previous_;
        box_ = session_.Document().timeline.editMode == 1;
        if (box_) {
            boxModel_ = session_.Selected().model;
            boxAdditive_ = e->modifiers() & Qt::ShiftModifier;
            frameCached_ = false;
            if (isExposed())
                setMouseGrabEnabled(true);
            if (changed)
                changed(true, {});
        }
        if (session_.Document().timeline.editMode == 0) {
            const bool ok = session_.PickBone(bonePoints_, DevicePoint(previous_).x(),
                                              DevicePoint(previous_).y(),
                                              e->modifiers() & Qt::ShiftModifier, reason);
            if (changed)
                changed(ok, reason);
        }
    }
}
void Viewport::mouseMoveEvent(QMouseEvent* e) {
    if (!pressed_) {
        const auto operation = ToolAt(e->position().toPoint());
        if (operation != toolOperation_) {
            toolOperation_ = operation;
            if (changed)
                changed(true, {});
        }
        return;
    }
    auto point = e->position().toPoint();
    const auto delta = DevicePoint(point) - DevicePoint(previous_);
    if (headerDragOperation_ || sceneDragMode_ || boneDragMode_) {
        if (!(e->buttons() & Qt::LeftButton)) {
            CancelInput();
            return;
        }
        // A pending/active Wayland request consumes only relative protocol
        // motion. Do not count its matching absolute Qt event a second time.
        if (!relativePointer_ || !relativePointer_->Requested())
            ToolMotion(DevicePoint(point), e->modifiers());
        return;
    }
    previous_ = point;
    if (box_ && (e->buttons() & Qt::LeftButton)) {
        std::string reason;
        if (session_.Playing() || session_.Selected().model != boxModel_) {
            FinishBoxSelection();
            return;
        }
        boxAdditive_ = e->modifiers() & Qt::ShiftModifier;
        frameCached_ = false;
        const auto bounds = QRect(DevicePoint(anchor_), DevicePoint(point)).normalized();
        const bool ok = session_.BoxSelectBones(bonePoints_, bounds.left(), bounds.top(),
                                                bounds.right(), bounds.bottom(),
                                                e->modifiers() & Qt::ShiftModifier, false, reason);
        if (changed)
            changed(ok, reason);
        return;
    }
    if (drag)
        drag(delta, e->buttons(), e->modifiers());
}
void Viewport::ToolMotion(QPoint point, Qt::KeyboardModifiers modifiers) {
    const auto motion = point - toolPrevious_;
    const auto previous = toolPrevious_;
    toolPrevious_ = point;
    if (headerDragOperation_) {
        std::string reason;
        const bool ok = session_.DragViewTool(
            headerDragOperation_, motion.x(), motion.y(), modifiers & Qt::ShiftModifier,
            modifiers & Qt::ControlModifier,
            render::BackgroundViewScale(qRound(width() * devicePixelRatio())), reason);
        if (!ok)
            FinishToolDrag();
        if (changed)
            changed(ok, reason);
        return;
    }
    if (sceneDragMode_) {
        if (session_.Playing() || session_.Selected().model != 255) {
            FinishToolDrag();
            if (changed)
                changed(true, {});
            return;
        }
        std::string reason;
        const bool ok =
            session_.DragSceneAxis(sceneDragMode_, motion.y(), modifiers & Qt::ShiftModifier,
                                   modifiers & Qt::ControlModifier, reason);
        if (!ok)
            FinishToolDrag();
        if (changed)
            changed(ok, reason);
        return;
    }
    if (boneDragMode_) {
        std::string reason;
        if (session_.BoneDragMode() != unsigned(boneDragMode_) ||
            session_.Selected().model != dragModel_ || session_.Playing() ||
            session_.Runtime().Resource(dragModel_) != dragResource_) {
            FinishToolDrag();
            if (changed)
                changed(true, {});
            return;
        }
        if (!session_.PrepareView(reason) ||
            !render::SceneBoneScreenPoints(
                session_.Runtime(), dragModel_, unsigned(boneDrag_.viewWidth),
                unsigned(boneDrag_.viewHeight), bonePoints_, reason, dragModel_) ||
            ActiveBone() < 0 || unsigned(ActiveBone()) >= bonePoints_.size()) {
            FinishToolDrag();
            if (changed)
                changed(false, reason);
            return;
        }
        boneDrag_.clipW = bonePoints_[unsigned(ActiveBone())].clipW;
        boneDrag_.previousX = previous.x();
        boneDrag_.previousY = previous.y();
        boneDrag_.x = point.x();
        boneDrag_.y = point.y();
        boneDrag_.shift = modifiers & Qt::ShiftModifier;
        boneDrag_.control = modifiers & Qt::ControlModifier;
        const bool ok = session_.DragBones(boneDrag_, reason);
        if (!ok)
            FinishToolDrag();
        if (changed)
            changed(ok, reason);
        return;
    }
}
void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if ((boneDragMode_ || sceneDragMode_ || headerDragOperation_) &&
        e->button() == Qt::LeftButton) {
        FinishToolDrag(true);
        if (changed)
            changed(true, {});
        return;
    }
    if (box_ && e->button() == Qt::LeftButton) {
        previous_ = e->position().toPoint();
        boxAdditive_ = e->modifiers() & Qt::ShiftModifier;
        FinishBoxSelection();
    }
    pressed_ = false;
}
void Viewport::wheelEvent(QWheelEvent* e) {
    if (zoom)
        zoom(e->angleDelta().y());
}
bool RenderImage(const runtime::SceneRuntime& scene, const editor::EditorAssets& assets,
                 const std::filesystem::path& builtins, const rhi::BackendOptions& options,
                 unsigned width, unsigned height, QImage& image, std::string& reason) {
    auto backend = rhi::CreateOffscreenBackendDevice(width, height, options);
    if (!backend) {
        reason = backend.reason;
        return false;
    }
    rhi::FileAssetSource files(builtins);
    render::SceneFrameProducer producer;
    if (!producer.Load(scene, Sources(assets), files, reason))
        return false;
    render::SceneRenderSettings settings;
    settings.width = width;
    settings.height = height;
    rhi::RenderFrame frame;
    if (!producer.Build(scene, settings, frame, reason))
        return false;
    rhi::SceneRenderer renderer(*backend.device);
    rhi::PreparedModelFrame prepared;
    rhi::CompletionToken token;
    auto result = renderer.Prepare(1, frame, prepared);
    if (result)
        result = renderer.Submit(prepared, token);
    if (!result) {
        reason = result.reason;
        return false;
    }
    rhi::AttachmentReadback pixels;
    if (!renderer.ReadTarget(1, frame.finalColor, pixels)) {
        reason = "export-readback-failed";
        return false;
    }
    image = QImage(pixels.bytes.data(), int(width), int(height), int(width * 4),
                   QImage::Format_RGBA8888)
                .copy();
    return !image.isNull();
}
}
