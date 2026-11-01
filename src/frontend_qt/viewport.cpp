#include "viewport.hpp"
#include <QPlatformSurfaceEvent>
#include <QMouseEvent>
#include <QWheelEvent>
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
    options_.backend = rhi::Backend::Vulkan;
    options_.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options_.driver = rhi::BackendDriver::Software;
}
Viewport::~Viewport() {
    controller_.Close();
}
void Viewport::SetOptions(const rhi::BackendOptions& options) {
    frameCached_ = false;
    controller_.Close();
    producer_.ResetHistory();
    options_ = options;
}
bool Viewport::Reset(std::string& reason) {
    frameCached_ = false;
    producer_.ResetHistory();
    if (!controller_.ReplaceDevice()) {
        reason = controller_.Reason();
        return false;
    }
    return true;
}
bool Viewport::Render(std::string& reason, bool hiddenAcceptance) {
    if ((!isExposed() && !hiddenAcceptance) || width() < 1 || height() < 1)
        return true;
    if (!session_.PrepareView(reason))
        return false;
    if (!overlay_.Load(builtins_, reason))
        return false;
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
        cachedEditMode_ != session_.Document().timeline.editMode) {
        frameCached_ = false;
        if (!producer_.Build(session_.Runtime(), settings, cachedFrame_, reason))
            return false;
        if (!overlay_.Append(session_.Runtime(), settings, session_.Selected().bones,
                             session_.Playing(), cachedFrame_, reason))
            return false;
        ++frameBuilds_;
        cachedRevision_ = revision;
        cachedUpdates_ = updates;
        cachedWidth_ = w;
        cachedHeight_ = h;
        cachedModel_ = session_.Selected().model;
        cachedBone_ = ActiveBone();
        cachedSelectedBones_ = session_.Selected().bones;
        cachedEditMode_ = session_.Document().timeline.editMode;
        cachedPlaying_ = session_.Playing();
        frameCached_ = !history;
    }
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
int Viewport::ActiveBone() const {
    return session_.ActiveBone();
}
std::uint64_t Viewport::Submissions() const {
    const auto* renderer = const_cast<rhi::ScenePreviewController&>(controller_).GetRenderer();
    return renderer ? renderer->Stats().submissions : 0;
}
bool Viewport::event(QEvent* event) {
    if (event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        frameCached_ = false;
        controller_.Close();
        producer_.ResetHistory();
        options_.surface.reset();
    }
    return QWindow::event(event);
}
void Viewport::mousePressEvent(QMouseEvent* e) {
    pressed_ = true;
    previous_ = e->position().toPoint();
    if (e->button() == Qt::LeftButton && session_.Selected().model != 255 && !session_.Playing()) {
        std::string reason;
        if (!session_.PrepareView(reason)) {
            if (changed)
                changed(false, reason);
            return;
        }
        if (!render::SceneBoneScreenPoints(session_.Runtime(), session_.Selected().model,
                                           unsigned(width()), unsigned(height()), bonePoints_,
                                           reason, session_.Selected().model)) {
            if (changed)
                changed(false, reason);
            return;
        }
        anchor_ = previous_;
        box_ = session_.Document().timeline.editMode == 1;
        if (session_.Document().timeline.editMode == 0) {
            const bool ok = session_.PickBone(bonePoints_, previous_.x(), previous_.y(),
                                              e->modifiers() & Qt::ShiftModifier, reason);
            if (changed)
                changed(ok, reason);
        }
    }
}
void Viewport::mouseMoveEvent(QMouseEvent* e) {
    if (!pressed_)
        return;
    auto point = e->position().toPoint();
    auto delta = point - previous_;
    previous_ = point;
    if (box_ && (e->buttons() & Qt::LeftButton)) {
        std::string reason;
        const auto bounds = QRect(anchor_, point).normalized();
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
void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (box_ && e->button() == Qt::LeftButton) {
        std::string reason;
        const auto bounds = QRect(anchor_, e->position().toPoint()).normalized();
        const bool ok = session_.BoxSelectBones(bonePoints_, bounds.left(), bounds.top(),
                                                bounds.right(), bounds.bottom(),
                                                e->modifiers() & Qt::ShiftModifier, true, reason);
        if (changed)
            changed(ok, reason);
        box_ = false;
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
