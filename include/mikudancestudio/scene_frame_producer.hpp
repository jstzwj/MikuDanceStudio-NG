#pragma once
#include "scene_runtime.hpp"
#include "rhi_frame_passes.hpp"
#include "render_self_shadow.hpp"
#include "bone_selection.hpp"
#include <filesystem>
namespace mikudancestudio::render {
struct SceneRenderSources {
    std::map<std::uint8_t, std::filesystem::path> models, accessories;
    std::filesystem::path sharedToons;
    std::shared_ptr<const rhi::RgbaImage> picture;
};
struct SceneRenderSettings {
    unsigned width = 640, height = 480;
    SelfShadowSettings shadow;
    bool postProcessCopy = true;
    // Absent: timeline/export convention. Present: native editor mode; 255
    // selects the camera panel, other values select the model reference pivot.
    std::optional<std::uint8_t> editorModel;
};
bool SceneViewInputs(const runtime::SceneRuntime&, const SceneRenderSettings&, ModelFrameInputs&,
                     std::string&);
// Uses the same camera inputs as scene rendering, in PMM bone-table order.
// No GPU readback or additional scene evaluation is involved.
bool SceneBoneScreenPoints(const runtime::SceneRuntime&, std::uint8_t slot, unsigned width,
                           unsigned height, std::vector<cpu_bone::ScreenPoint>&, std::string&,
                           std::optional<std::uint8_t> editorModel = std::nullopt);
// Consumes an already published P2 scene snapshot. Never advances animation or
// physics and never borrows native GPU objects. GPU caches survive resize;
// changed CPU bytes retain resource IDs and receive increasing versions.
class SceneFrameProducer final {
public:
    SceneFrameProducer();
    ~SceneFrameProducer();
    bool Load(const runtime::SceneRuntime&, const SceneRenderSources&, const rhi::AssetSource&,
              std::string& reason);
    bool Build(const runtime::SceneRuntime&, const SceneRenderSettings&, rhi::RenderFrame&,
               std::string& reason);
    // Advance screen.bmp and stock-effect state only after successful submission.
    // True invalidates a cached frame whose inherited material state changed.
    bool Commit();
    bool HasMaterialHistory() const;
    void ResetHistory();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t nextId_ = 0x100000000ull;
};
}
