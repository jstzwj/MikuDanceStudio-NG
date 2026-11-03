#pragma once
#include "scene_frame_producer.hpp"
#include "viewport_tools.hpp"

namespace mikudancestudio::render {
// Editor-only final composition. Append after scene history capture; exports
// never instantiate this producer. Resource identities survive hiding, playback,
// resize and device replacement, including frames with no overlay draws.
// Pure clear-only frames release leases and require new versions on revival.
class SceneEditorOverlay final {
public:
    bool Load(const rhi::AssetSource&, std::string&);
    bool Append(const runtime::SceneRuntime&, const SceneRenderSettings&,
                const std::vector<std::uint32_t>& selected, bool playing, rhi::RenderFrame&,
                std::string&, const ViewportTools* tools = nullptr);
    // Commit native gizmo color history only after successful submission.
    bool Commit() {
        const bool changed = debugColor_ != pendingDebugColor_;
        debugColor_ = pendingDebugColor_;
        return changed;
    }

private:
    rhi::ModelReplayResource texture_, lines_, sprites_, axisVertices_, axisIndices_;
    rhi::ModelReplayResource physicsVertices_, physicsIndices_;
    PhysicsGizmoRanges physicsRanges_{};
    std::uint32_t debugColor_ = 0xffff0000, pendingDebugColor_ = 0xffff0000;
    std::shared_ptr<const runtime::AccessoryResource> axis_;
    bool loaded_ = false;
    bool retired_ = false;
};
}
