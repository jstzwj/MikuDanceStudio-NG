#pragma once
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include <QWindow>
#include <QImage>
#include <functional>
namespace mikudancestudio::qt {
std::shared_ptr<const rhi::VulkanSurfaceSource> QtSurface(QWindow&, std::string&);
class Viewport final : public QWindow {
public:
    explicit Viewport(editor::EditorSession&, const std::filesystem::path& assets);
    ~Viewport() override;
    bool Render(std::string&, bool hiddenAcceptance = false);
    void SetOptions(const rhi::BackendOptions&);
    const rhi::BackendOptions& Options() const { return options_; }
    bool Reset(std::string&);
    std::function<void(QPoint, Qt::MouseButtons, Qt::KeyboardModifiers)> drag;
    std::function<void(int)> zoom;
    std::function<void(QPoint)> pick;
    std::uint64_t Submissions() const;

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
    rhi::FileAssetSource builtins_;
    editor::EditorAssets loaded_;
    bool loadedAssets_ = false, pressed_ = false;
    QPoint previous_;
};
// Export uses its own scene/renderer. It never reads the editor swapchain.
bool RenderImage(const runtime::SceneRuntime&, const editor::EditorAssets&,
                 const std::filesystem::path& builtins, const rhi::BackendOptions&, unsigned width,
                 unsigned height, QImage&, std::string&);
}
