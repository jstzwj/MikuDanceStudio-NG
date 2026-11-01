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
    controller_.Close();
    options_ = options;
}
bool Viewport::Reset(std::string& reason) {
    if (!controller_.ReplaceDevice()) {
        reason = controller_.Reason();
        return false;
    }
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
    const auto& assets = session_.Assets();
    if (!loadedAssets_ || assets.models != loaded_.models ||
        assets.accessories != loaded_.accessories || assets.modelPaths != loaded_.modelPaths ||
        assets.accessoryPaths != loaded_.accessoryPaths) {
        if (!producer_.Load(session_.Runtime(), Sources(assets), builtins_, reason))
            return false;
        loaded_ = assets;
        loadedAssets_ = true;
    }
    render::SceneRenderSettings settings;
    settings.width = w;
    settings.height = h;
    rhi::RenderFrame frame;
    if (!producer_.Build(session_.Runtime(), settings, frame, reason))
        return false;
    if (!controller_.Draw(frame)) {
        reason = controller_.Reason();
        return false;
    }
    reason.clear();
    return true;
}
std::uint64_t Viewport::Submissions() const {
    const auto* renderer = const_cast<rhi::ScenePreviewController&>(controller_).GetRenderer();
    return renderer ? renderer->Stats().submissions : 0;
}
bool Viewport::event(QEvent* event) {
    if (event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        controller_.Close();
        options_.surface.reset();
    }
    return QWindow::event(event);
}
void Viewport::mousePressEvent(QMouseEvent* e) {
    pressed_ = true;
    previous_ = e->position().toPoint();
    if (e->button() == Qt::LeftButton && pick)
        pick(previous_);
}
void Viewport::mouseMoveEvent(QMouseEvent* e) {
    if (!pressed_)
        return;
    auto point = e->position().toPoint();
    auto delta = point - previous_;
    previous_ = point;
    if (drag)
        drag(delta, e->buttons(), e->modifiers());
}
void Viewport::mouseReleaseEvent(QMouseEvent*) {
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
