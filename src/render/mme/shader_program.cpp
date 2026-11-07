#include "mikudancestudio/mme_render.hpp"
#include "shader_state.hpp"
#include <algorithm>
#include <deque>

namespace mikudancestudio::mme {
namespace {
using namespace rhi;
ShaderValueType ValueType(const MmeLegacyVariable& v) {
    if (v.type == "bool")
        return ShaderValueType::UInt;
    if (v.type == "int")
        return ShaderValueType::Int;
    if (v.rows == 4)
        return ShaderValueType::Matrix4x4;
    return v.columns == 1   ? ShaderValueType::Float
           : v.columns == 2 ? ShaderValueType::Float2
           : v.columns == 3 ? ShaderValueType::Float3
                            : ShaderValueType::Float4;
}
std::string CacheKey(const EffectDefinition& d, std::uint64_t revision, bool scene,
                     const EffectCompilerOptions& options) {
    std::string key;
    const auto add = [&](const std::string& s) { key += std::to_string(s.size()) + ":" + s; };
    add("fx-frontend-4/explicit-packoffset-1/portable-trilinear-1/dxc-vulkan13-dxlayout-zpr-ges-gis");
    add(options.backend);
    add(scene ? "scene" : "material");
    add(options.compiler->Identity());
    add(std::to_string(revision));
    add(d.source.preprocessorIdentity);
    add(d.source.expanded);
    add(std::to_string(d.source.files.size()));
    for (const auto& file : d.source.files) {
        add(file.path.u8string());
        add(file.encoding);
        add(file.utf8);
    }
    add(std::to_string(d.source.macros.size()));
    for (const auto& [name, value] : d.source.macros) {
        add(name);
        add(value);
    }
    add(std::to_string(d.source.includes.size()));
    for (const auto& edge : d.source.includes) {
        add(std::to_string(edge.parent));
        add(std::to_string(edge.child));
        add(std::to_string(edge.line));
    }
    return key;
}
bool CompilePass(const MmeLegacyProgram& modern, unsigned index, const CompiledEffect& effect,
                 SpirvCompiler& compiler, CompiledPass& out, std::string& reason) {
    const auto& pass = modern.passes.at(index);
    BatchRasterState raster;
    if (!detail::ShaderState(pass, !modern.scene, {}, raster, reason))
        return false;
    ShaderProgramDesc desc;
    desc.name = pass.technique + "/" + pass.name;
    std::vector<MmeLegacyInput> inputs;
    if (!MmeLegacyShader(modern, pass, desc.vertex.hlsl, inputs, reason, &desc.colorOutputs))
        return false;
    desc.pixel.hlsl = desc.vertex.hlsl;
    desc.vertex.entry = "mdsLegacyVS";
    desc.pixel.entry = "mdsLegacyPS";
    for (const auto& i : inputs) {
        const bool uv = i.semantic == "TEXCOORD0",
                   normal = i.semantic == "NORMAL" || i.semantic == "NORMAL0";
        desc.inputs.push_back({uv       ? "TEXCOORD"
                               : normal ? "NORMAL"
                                        : "POSITION",
                               0, unsigned(desc.inputs.size()),
                               uv       ? 24u
                               : normal ? 12u
                                        : 0u,
                               uv ? ShaderVertexFormat::Float2 : ShaderVertexFormat::Float3});
    }
    desc.bindings.push_back({ShaderBindingKind::Constants,
                             0,
                             1,
                             16,
                             {{"mdsLegacyViewport", ShaderValueType::Float4, 0}}});
    if (effect.constantBytes) {
        ShaderBinding block{ShaderBindingKind::Constants, 1, 3, effect.constantBytes};
        for (const auto& m : effect.layout) {
            const auto& v = modern.variables.at(m.variable);
            block.members.push_back({v.name, ValueType(v), m.offset, v.elements});
        }
        desc.bindings.push_back(std::move(block));
    }
    if (!MmeLegacySamplers(modern, pass, true).empty()) {
        reason = "MME_shader_vertex_texture_not_supported";
        return false;
    }
    out.samplers = MmeLegacySamplers(modern, pass);
    for (auto slot : out.samplers) {
        desc.bindings.push_back({ShaderBindingKind::Texture, slot});
        desc.bindings.push_back({ShaderBindingKind::Sampler, slot});
    }
    if (!compiler.Compile(desc.vertex, desc.vertex.spirv, reason) ||
        !compiler.Compile(desc.pixel, desc.pixel.spirv, reason))
        return false;
    const auto reflected = ValidateSpirvProgram(desc);
    if (!reflected) {
        reason = reflected.reason;
        return false;
    }
    ShaderPipeline pipeline;
    pipeline.program = std::make_shared<const ShaderProgram>(std::move(desc));
    pipeline.colorFormats.assign(pipeline.program->description.colorOutputs.size(),
                                 TextureFormat::Rgba8);
    auto capabilities = GraphicsShaderProfile(true, true);
    capabilities.maxColorAttachments = 4;
    const auto valid = ValidateShaderPipeline(pipeline, capabilities);
    if (!valid) {
        reason = valid.reason;
        return false;
    }
    out.program = std::move(pipeline.program);
    reason.clear();
    return true;
}
}
struct EffectCompiler::Impl {
    EffectCompilerOptions options;
    struct Entry {
        std::shared_ptr<const CompiledEffect> effect;
        std::string reason;
        std::size_t bytes = 0;
    };
    std::map<std::string, Entry> cache;
    std::deque<std::string> order;
    EffectCompilerStats stats;
    std::size_t cachedBytes = 0;
};
EffectCompiler::EffectCompiler(EffectCompilerOptions options) : impl_(new Impl) {
    if (!options.compiler)
        options.compiler = rhi::DefaultSpirvCompiler();
    options.maxEntries = std::min<std::size_t>(options.maxEntries, 128);
    options.cacheBytes = std::min<std::size_t>(options.cacheBytes, 512 * 1024 * 1024);
    impl_->options = std::move(options);
}
EffectCompiler::~EffectCompiler() = default;
EffectCompilerStats EffectCompiler::Stats() const {
    return impl_->stats;
}
void EffectCompiler::Clear() {
    impl_->cache.clear();
    impl_->order.clear();
    impl_->cachedBytes = 0;
}
bool EffectCompiler::Compile(const ScheduleDescription& description,
                             std::shared_ptr<const CompiledEffect>& output, std::string& reason) {
    if (!description.binding.instance ||
        description.binding.instance->Definition().source.expanded.empty()) {
        reason = "MME_shader_frozen_source_required";
        return false;
    }
    auto& state = *impl_;
    const auto def = description.binding.instance->DefinitionSnapshot();
    const auto key = CacheKey(*def, description.binding.revision, description.scene, state.options);
    const auto found = state.cache.find(key);
    if (found != state.cache.end()) {
        ++state.stats.cacheHits;
        reason = found->second.reason;
        if (!found->second.effect)
            return false;
        auto rebound = std::make_shared<CompiledEffect>(*found->second.effect);
        rebound->definition = def;
        output = std::move(rebound);
        return true;
    }
    ++state.stats.effects;
    auto next = std::make_shared<CompiledEffect>();
    next->definition = def;
    MmeLegacyProgram modern;
    bool success =
        MmeLegacyGenerate(def->metadata, modern, next->layout, next->constantBytes, reason);
    modern.scene = description.scene;
    if (success) {
        next->passes.resize(modern.passes.size());
        for (unsigned i = 0; i < modern.passes.size(); ++i) {
            auto& pass = next->passes[i];
            if (!CompilePass(modern, i, *next, *state.options.compiler, pass, pass.diagnostic))
                ++state.stats.failedPasses;
        }
        for (const auto& technique : modern.techniques) {
            bool valid = true;
            for (auto index : technique.passes)
                valid &= index < next->passes.size() && bool(next->passes[index].program);
            next->techniques.push_back(valid);
        }
        reason.clear();
    }
    std::size_t bytes = key.size() * 2 + reason.size();
    for (const auto& pass : next->passes) {
        bytes += pass.diagnostic.size();
        if (pass.program) {
            const auto& d = pass.program->description;
            bytes += pass.program->key.size() + d.vertex.hlsl.size() + d.pixel.hlsl.size() +
                     d.vertex.spirv.size() + d.pixel.spirv.size();
        }
    }
    if (state.options.maxEntries && bytes <= state.options.cacheBytes) {
        while (state.order.size() >= state.options.maxEntries ||
               state.cachedBytes + bytes > state.options.cacheBytes) {
            const auto old = state.cache.find(state.order.front());
            state.cachedBytes -= old->second.bytes;
            state.cache.erase(old);
            state.order.pop_front();
        }
        state.cachedBytes += bytes;
        state.order.push_back(key);
        state.cache.emplace(key, Impl::Entry{success ? next : nullptr, reason, bytes});
    }
    if (success)
        output = std::move(next);
    return success;
}
}
