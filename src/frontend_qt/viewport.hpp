#pragma once
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/scene_editor_overlay.hpp"
#include "relative_pointer.hpp"
#include <QWindow>
#include <QImage>
#include <functional>
namespace mikudancestudio::qt {
std::shared_ptr<const rhi::WindowSurfaceSource> QtSurface(QWindow&, std::string&);
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
    bool OutputLimits(QSize&, std::string&) const;
    std::function<void(QPoint, Qt::MouseButtons, Qt::KeyboardModifiers)> drag;
    std::function<void(int)> zoom;
    std::function<void(bool, const std::string&)> changed;
    std::uint64_t Submissions() const;
    std::uint64_t FrameBuilds() const { return frameBuilds_; }
    bool BeginHeaderDrag(unsigned operation, QPoint position, QPoint restore,
                         QWindow* inputSurface = nullptr);
    bool HeaderDragging() const { return headerDragOperation_ != 0; }
    void CancelInput();

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
    render::SceneEditorOverlay overlay_;
    rhi::FileAssetSource builtins_;
    editor::EditorAssets loaded_;
    rhi::RenderFrame cachedFrame_;
    std::uint64_t cachedRevision_ = 0, cachedUpdates_ = 0, frameBuilds_ = 0;
    unsigned cachedWidth_ = 0, cachedHeight_ = 0;
    std::uint8_t cachedModel_ = 255;
    int cachedBone_ = -1;
    std::vector<std::uint32_t> cachedSelectedBones_;
    int cachedEditMode_ = -1;
    unsigned cachedCoordinateSystem_ = 0;
    int toolOperation_ = 0, cachedToolOperation_ = -1;
    bool cachedPlaying_ = false;
    int ActiveBone() const;
    int ToolAt(QPoint);
    bool frameCached_ = false;
    bool profileFrame_ = false;
    FrameCpuTimings cpuTimings_;
    bool loadedAssets_ = false, pressed_ = false;
    int boneDragMode_ = 0;
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
    QPoint DevicePoint(QPoint) const;
    void FinishBoxSelection();
    bool box_ = false, boxAdditive_ = false;
    std::uint8_t boxModel_ = 255;
    QPoint anchor_;
    std::vector<cpu_bone::ScreenPoint> bonePoints_;
    QPoint previous_;
};
// Export uses its own scene/renderer. It never reads the editor swapchain.
bool RenderImage(const runtime::SceneRuntime&, const editor::EditorAssets&,
                 const std::filesystem::path& builtins, const rhi::BackendOptions&, unsigned width,
                 unsigned height, QImage&, std::string&);
}
