#pragma once
#include "editor_effects.hpp"
#include "mme_render.hpp"
#include "scene_frame_producer.hpp"
namespace mikudancestudio::render {
// Render-owner service. Editor documents contain only source references and
// logical overrides; effects, pools, compiled programs and pending writes live here.
class SceneEffects final {
public:
    SceneEffects();
    void SetSourceOptions(mme::SourceOptions);
    ~SceneEffects();
    bool Build(const editor::EffectDocument&, std::uint64_t documentRevision,
               const runtime::SceneRuntime&, const SceneRenderSources&, const SceneRenderSettings&,
               const rhi::BackendOptions&, const rhi::RenderFrame& base, rhi::RenderFrame&,
               std::string&);
    bool Commit(std::string&);
    void Abandon();
    void Reset();
    // End leases when the native device or renderer scene lifetime changes;
    // keep committed logical parameters and the immutable compiled sources.
    void InvalidateTargets();
    const std::string& Report() const;
    std::shared_ptr<const mme::EffectDefinition> Definition(editor::EffectAddress) const;
    std::shared_ptr<const mme::EffectDefinition>
    Inspect(editor::EffectAddress, const editor::EffectDocument&, std::uint64_t revision);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
