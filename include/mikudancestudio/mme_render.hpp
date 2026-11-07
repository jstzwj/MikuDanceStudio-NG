#pragma once
#include "mme_schedule.hpp"
#include "shader_compiler.hpp"

namespace mikudancestudio::mme {
struct CompiledPass {
    std::shared_ptr<const rhi::ShaderProgram> program;
    std::vector<unsigned> samplers;
    std::string diagnostic;
};
struct CompiledEffect {
    std::shared_ptr<const EffectDefinition> definition;
    std::vector<MmeLegacyConstant> layout;
    unsigned constantBytes = 0;
    std::vector<CompiledPass> passes;
    std::vector<bool> techniques;
};
struct EffectCompilerOptions {
    std::shared_ptr<rhi::SpirvCompiler> compiler;
    // Backend/capability policy is part of the cache identity. Programs still
    // contain the same HLSL and SPIR-V; native objects remain device-owned.
    std::string backend;
    std::size_t maxEntries = 32;
    std::size_t cacheBytes = 64 * 1024 * 1024;
};
struct EffectCompilerStats {
    std::uint64_t effects = 0, cacheHits = 0, failedPasses = 0;
};
class EffectCompiler {
public:
    explicit EffectCompiler(EffectCompilerOptions);
    ~EffectCompiler();
    // Each technique records actual shader validity, including cached failures.
    // An invalid unused technique need not prevent a later valid fallback.
    bool Compile(const ScheduleDescription&, std::shared_ptr<const CompiledEffect>&, std::string&);
    EffectCompilerStats Stats() const;
    void Clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
using CompiledEffects = std::map<std::uint64_t, std::shared_ptr<const CompiledEffect>>;
struct RenderGeometry {
    std::vector<rhi::ModelReplayResource> resources;
    std::map<std::pair<std::uint64_t, int>, rhi::ModelReplayDraw> draws;
};
// Pure assembly; no GPU allocation, submission, clock resampling or parameter
// writes. The host commits the schedule only after successful submission.
// Failure preserves output. Registry/parameter revisions are checked twice.
bool AssembleRenderFrame(const ScheduledFrame&, const CompiledEffects&, const TargetRegistry&,
                         const RenderGeometry&, rhi::RenderFrame&, std::string&);
}
