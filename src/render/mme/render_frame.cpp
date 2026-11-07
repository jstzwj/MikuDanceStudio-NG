#include "mikudancestudio/mme_render.hpp"
#include "shader_state.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace mikudancestudio::mme {
namespace {
using namespace rhi;
[[noreturn]] void Fail(const std::string& reason) {
    throw std::runtime_error(reason);
}
const FrameTarget& Target(const RenderFrame& frame, std::uint64_t id) {
    for (const auto& t : frame.targets)
        if (t.id == id)
            return t;
    Fail("MME_render_attachment_missing");
}
SnapshotBytes Bytes(const void* data, std::size_t size) {
    SnapshotBytes bytes;
    bytes.resize(size);
    std::memcpy(bytes.data(), data, size);
    return bytes;
}
ModelReplayDraw Quad(RenderFrame& frame, unsigned width, unsigned height) {
    // Match the native FX9 Draw=Buffer quad; the wrapper applies the API's
    // half-pixel correction separately. UV compensation belongs to the FX.
    const float left = -1 - 2.f / width, top = 1 + 2.f / height, u = -1.f / width,
                v = -1.f / height;
    const float vertices[][8] = {{1, -1, 0, 0, 0, 0, 1, 1},     {left, -1, 0, 0, 0, 0, u, 1},
                                 {left, top, 0, 0, 0, 0, u, v}, {1, -1, 0, 0, 0, 0, 1, 1},
                                 {left, top, 0, 0, 0, 0, u, v}, {1, top, 0, 0, 0, 0, 1, v}};
    ModelReplayResource resource;
    resource.key = {UINT64_MAX - (std::uint64_t(width) << 32) - height, 1};
    resource.bytes = Bytes(vertices, sizeof(vertices));
    const auto found = std::find_if(frame.resources.begin(), frame.resources.end(),
                                    [&](const auto& r) { return r.key.id == resource.key.id; });
    if (found == frame.resources.end())
        frame.resources.push_back(resource);
    else if (found->kind != resource.kind || found->bytes != resource.bytes ||
             found->key.version != resource.key.version)
        Fail("MME_render_quad_resource_collision");
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = resource.key;
    draw.drawVertexCount = 6;
    return draw;
}
void Constants(const CompiledEffect& effect, const ParameterSnapshot& snapshot,
               ReplayShader& shader, unsigned width, unsigned height) {
    const float viewport[] = {1.f / width, 1.f / height, float(width), float(height)};
    shader.constants.push_back({0, Bytes(viewport, sizeof(viewport))});
    if (!effect.constantBytes)
        return;
    ShaderConstantBinding block{1};
    block.bytes.resize(effect.constantBytes);
    for (const auto& member : effect.layout) {
        const auto& v = effect.definition->metadata.variables.at(member.variable);
        const auto found = snapshot.values.find(v.name);
        if (found == snapshot.values.end())
            Fail("MME_render_parameter_missing:" + v.name);
        const auto& value = found->second;
        const auto type = v.type == "bool"  ? ParameterValue::Type::Bool
                          : v.type == "int" ? ParameterValue::Type::Int
                                            : ParameterValue::Type::Float;
        if (value.type != type || value.rows != v.rows || value.columns != v.columns ||
            value.elements != v.elements || value.array != v.array ||
            value.words.size() != std::size_t(v.rows) * v.columns * v.elements)
            Fail("MME_render_parameter_shape:" + v.name);
        const auto size = v.rows * v.columns * 4;
        for (unsigned n = 0; n < v.elements; ++n) {
            if (member.offset + n * member.stride + size > block.bytes.size())
                Fail("MME_render_parameter_bounds");
            std::memcpy(block.bytes.data() + member.offset + n * member.stride,
                        value.words.data() + std::size_t(n) * v.rows * v.columns, size);
        }
    }
    shader.constants.push_back(std::move(block));
}
void Textures(std::uint64_t id, const CompiledEffect& effect, const CompiledPass& program,
              const TargetRegistry& registry, const RenderFrame& frame, const ModelReplayDraw& host,
              ReplayShader& shader) {
    for (auto slot : program.samplers) {
        const auto& sampler = effect.definition->metadata.samplers.at(slot);
        const auto t = std::find_if(effect.definition->metadata.textures.begin(),
                                    effect.definition->metadata.textures.end(),
                                    [&](const auto& t) { return t.name == sampler.texture; });
        if (t == effect.definition->metadata.textures.end())
            Fail("MME_render_sampler_texture_missing");
        ReplayShaderTextureBinding binding;
        binding.slot = slot;
        if (t->semantic.empty() || t->semantic == "RENDERCOLORTARGET" ||
            t->semantic == "OFFSCREENRENDERTARGET") {
            TargetLease lease;
            FrameTarget target;
            std::string reason;
            if (!registry.Resolve(id, t->name, lease, target, reason) ||
                !registry.Validate(lease, reason))
                Fail(reason);
            const auto& actual = Target(frame, target.id).description;
            if (actual.width != target.description.width ||
                actual.height != target.description.height ||
                actual.format != target.description.format)
                Fail("MME_render_target_revision");
            if (sampler.mipNone && actual.mips != 1)
                Fail("MME_render_mipped_sampler_none");
            binding.target = {target.id, actual.width, actual.height};
        } else {
            const auto role = t->semantic == "MATERIALTEXTURE"       ? MaterialTextureRole::Base
                              : t->semantic == "MATERIALSPHEREMAP"   ? MaterialTextureRole::Sphere
                              : t->semantic == "MATERIALTOONTEXTURE" ? MaterialTextureRole::Toon
                                                                     : MaterialTextureRole::None;
            if (role == MaterialTextureRole::None)
                Fail("MME_render_texture_semantic");
            unsigned index = 3;
            for (unsigned n = 0; n < 3; ++n)
                if (host.pipeline.shader.layout.stages[n].role == role)
                    index = n;
            if (index == 3 || host.pipeline.shader.srgbSample[index])
                Fail("MME_render_material_texture_missing");
            binding.texture = host.textures[index];
            binding.target = host.targetTextures[index];
            if (sampler.mipNone && binding.target.target &&
                Target(frame, binding.target.target).description.mips != 1)
                Fail("MME_render_mipped_sampler_none");
            if (sampler.mipNone && binding.texture.id) {
                const auto upload = std::find_if(
                    frame.resources.begin(), frame.resources.end(), [&](const auto& r) {
                        return r.key.id == binding.texture.id &&
                               r.key.version == binding.texture.version;
                    });
                if (upload != frame.resources.end() && upload->texture.levels.size() != 1)
                    Fail("MME_render_mipped_sampler_none");
            }
        }
        shader.textures.push_back(binding);
        shader.samplers.push_back({slot, detail::ShaderSampler(sampler)});
    }
}
}
bool AssembleRenderFrame(const ScheduledFrame& schedule, const CompiledEffects& effects,
                         const TargetRegistry& registry, const RenderGeometry& geometry,
                         rhi::RenderFrame& output, std::string& reason) try {
    if (!schedule.Ready(reason))
        return false;
    auto next = schedule.Dependencies();
    next.resources = geometry.resources;
    next.passes.clear();
    for (const auto& step : schedule.Steps()) {
        auto pass = step.resources;
        if (step.kind == PlannedStep::Kind::Clear || step.kind == PlannedStep::Kind::GenerateMips) {
            next.passes.push_back(std::move(pass));
            continue;
        }
        const auto& target = Target(next, pass.colors.at(0).target).description;
        ModelReplayDraw draw;
        const bool material = step.kind != PlannedStep::Kind::Buffer;
        if (material) {
            const auto host = geometry.draws.find({step.object, step.subset});
            if (host == geometry.draws.end())
                Fail("MME_render_geometry_missing");
            draw = host->second;
            if (draw.shader)
                Fail("MME_render_host_shader_ambiguous");
        } else
            draw = Quad(next, target.width, target.height);
        if (step.kind != PlannedStep::Kind::Host) {
            const auto e = effects.find(step.effect);
            if (e == effects.end() || !e->second)
                Fail("MME_render_effect_missing");
            const auto& effect = *e->second;
            if (effect.definition != step.parameters.definition)
                Fail("MME_render_definition_revision");
            if (step.technique >= effect.techniques.size() || !effect.techniques[step.technique] ||
                step.pass >= effect.passes.size() || !effect.passes[step.pass].program)
                Fail("MME_render_pass_not_compiled");
            const auto& program = effect.passes[step.pass];
            ReplayShader shader;
            shader.pipeline.program = program.program;
            shader.pipeline.vertexLayout =
                material ? draw.pipeline.vertexLayout : VertexLayout::PositionNormalUv;
            shader.pipeline.topology =
                material ? draw.pipeline.topology : PrimitiveTopology::TriangleList;
            shader.pipeline.raster = material ? draw.pipeline.raster : BatchRasterState{};
            if (!material) {
                shader.pipeline.raster.depthTest = shader.pipeline.raster.depthWrite =
                    shader.pipeline.raster.alphaBlend = false;
                shader.pipeline.raster.cull = BatchCull::None;
            }
            if (!detail::ShaderState(effect.definition->metadata.passes.at(step.pass), material,
                                     draw.pipeline, shader.pipeline.raster, reason))
                return false;
            shader.pipeline.colorFormats.clear();
            for (const auto& a : pass.colors)
                shader.pipeline.colorFormats.push_back(Target(next, a.target).description.format);
            shader.pipeline.viewport = ModelViewport{0, 0, target.width, target.height, 0, 1};
            Constants(effect, step.parameters, shader, target.width, target.height);
            Textures(step.effect, effect, program, registry, next, draw, shader);
            draw.shader = std::move(shader);
            draw.textures = {};
            draw.targetTextures = {};
            draw.shadowTarget = 0;
        }
        draw.pipeline.viewport = ModelViewport{0, 0, target.width, target.height, 0, 1};
        pass.draws.push_back(std::move(draw));
        next.passes.push_back(std::move(pass));
    }
    const auto valid = rhi::ValidateRenderFrame(next);
    if (!valid) {
        reason = valid.reason;
        return false;
    }
    if (!schedule.Ready(reason))
        return false;
    output = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
}
