#pragma once
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/scene_editor_overlay.hpp"
#include "mikudancestudio/scene_effects.hpp"
#include "relative_pointer.hpp"
#include <QWindow>
#include <QImage>
#include <functional>
namespace mikudancestudio::qt {
render::SceneRenderSources EditorRenderSources(const editor::EditorAssets&);
std::shared_ptr<const rhi::WindowSurfaceSource> QtSurface(QWindow&, std::string&);
#ifdef __ANDROID__
void ResizeAndroidSurface(QWindow&, QSize);
#endif
unsigned SceneAntialiasSamples(const rhi::Device&);
struct FrameCpuTimings {
    double view = 0, assets = 0, scene = 0, overlay = 0;
};
class Viewport final : public QWindow {
public:
    explicit Viewport(editor::EditorSession&, const std::filesystem::path& assets);
    ~Viewport() override;
    bool Render(std::string&, bool hiddenAcceptance = false);
    // Physical-pixel CPU preparation, also usable before a native surface
    // exists. This never creates a device, submits work or advances history.
    bool PrepareFrame(unsigned width, unsigned height, std::string&);
    const rhi::RenderFrame& PreparedFrame() const { return cachedFrame_; }
    void SetFrameProfiling(bool enabled) { profileFrame_ = enabled; }
    const FrameCpuTimings& CpuTimings() const { return cpuTimings_; }
    void SetOptions(const rhi::BackendOptions&);
    const rhi::BackendOptions& Options() const { return options_; }
    bool Reset(std::string&);
    bool ReloadTextures(std::string&);
    bool OutputLimits(QSize&, std::string&) const;
    unsigned AntialiasSamples() const;
    std::function<void(QPoint, Qt::MouseButtons, Qt::KeyboardModifiers)> drag;
    std::function<void(int)> zoom;
    std::function<bool()> presentationBlocked;
    std::function<void(bool, const std::string&)> changed;
    std::function<render::PhysicsEditView()> physicsEdit;
    std::function<bool(unsigned, int, bool)> physicsDrag;
    std::uint64_t Submissions() const;
    std::uint64_t FrameBuilds() const { return frameBuilds_; }
    rhi::PreviewState PresentationState() const { return controller_.State(); }
    const std::string& EffectReport() const {
        return deviceError_.empty() ? effects_.Report() : deviceError_;
    }
    std::shared_ptr<const mme::EffectDefinition> EffectDefinition(editor::EffectAddress a) {
        return effects_.Inspect(a, session_.Effects(), session_.EffectRevision());
    }
    const std::string& PresentationReason() const { return controller_.Reason(); }
    std::uint64_t SurfaceRebuilds() const;
    bool BeginHeaderDrag(unsigned operation, QPoint position, QPoint restore,
                         QWindow* inputSurface = nullptr);
    bool HeaderDragging() const { return headerDragOperation_ != 0; }
    void CancelInput();
#ifdef __ANDROID__
    QAccessibleInterface* accessibleRoot() const override;
#endif

protected:
    bool event(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    editor::EditorSession& session_;
    rhi::BackendOptions options_;
    rhi::ScenePreviewController controller_;
    render::SceneFrameProducer producer_;
    render::SceneEffects effects_;
    std::uint64_t cachedEffects_ = 0;
    std::string deviceError_;
    std::uint64_t failedRevision_ = 0, failedEffects_ = 0, failedUpdates_ = 0;
    unsigned failedWidth_ = 0, failedHeight_ = 0;
    void PresentError(unsigned width, unsigned height);
    render::SceneEditorOverlay overlay_;
    rhi::FileAssetSource builtins_;
    editor::EditorAssets loaded_;
    rhi::RenderFrame cachedFrame_;
    std::uint64_t cachedRevision_ = 0, cachedUpdates_ = 0, frameBuilds_ = 0;
    unsigned cachedWidth_ = 0, cachedHeight_ = 0, cachedSamples_ = 0;
    mutable unsigned supportedSamples_ = 0;
    std::uint8_t cachedModel_ = 255;
    int cachedBone_ = -1;
    std::vector<std::uint32_t> cachedSelectedBones_;
    int cachedEditMode_ = -1;
    unsigned cachedCoordinateSystem_ = 0;
    int toolOperation_ = 0, cachedToolOperation_ = -1;
    bool cachedPlaying_ = false;
    std::uint64_t cachedAppearanceRevision_ = 0;
    int ActiveBone() const;
    int ToolAt(QPointF);
    bool frameCached_ = false;
    bool profileFrame_ = false;
    FrameCpuTimings cpuTimings_;
    bool loadedAssets_ = false, pressed_ = false;
#ifdef __ANDROID__
    QSize androidSurfaceSize_;
#endif
    int boneDragMode_ = 0;
    unsigned physicsDragMode_ = 0;
    unsigned sceneDragMode_ = 0;
    unsigned headerDragOperation_ = 0;
    std::uint8_t dragModel_ = 255;
    std::shared_ptr<const runtime::ModelResource> dragResource_;
    editor::BoneDragInput boneDrag_;
    void FinishToolDrag(bool restorePosition = false);
    bool BeginCenteredCursor(int operation, QWindow* inputSurface = nullptr);
    void ToolMotion(QPoint physical, Qt::KeyboardModifiers);
    QPoint toolPrevious_;
    QPointF relativePosition_;
    std::unique_ptr<RelativePointer> relativePointer_;
    bool centeredCursor_ = false, cursorWarped_ = false;
    QPoint restoreCursor_;
    QCursor savedCursor_;
    QPoint DevicePoint(QPointF) const;
    void FinishBoxSelection();
    bool box_ = false, boxAdditive_ = false;
    std::uint8_t boxModel_ = 255;
    QPointF anchor_;
    std::vector<cpu_bone::ScreenPoint> bonePoints_;
    QPointF previous_;
    bool touchNavigation_ = false;
    QPointF touchCenter_;
    double touchSpan_ = 0;
};
// Export uses its own scene/renderer. It never reads the editor swapchain.
bool RenderImage(const runtime::SceneRuntime&, const editor::EditorAssets&,
                 const std::filesystem::path& builtins, const rhi::BackendOptions&, unsigned width,
                 unsigned height, QImage&, std::string&, const SceneDisplayOptions& = {},
                 const editor::EffectDocument& = {});
}
