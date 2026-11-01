#pragma once
#include "scene_frame_producer.hpp"

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
                std::string&);

private:
    rhi::ModelReplayResource texture_, lines_, sprites_;
    bool loaded_ = false;
    bool retired_ = false;
};
}
