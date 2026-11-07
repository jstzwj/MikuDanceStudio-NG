#pragma once
#include "mme_runtime_test_support.hpp"
#include "mikudancestudio/mme_render.hpp"
#include <fstream>
namespace mme_test {
using namespace mikudancestudio::rhi;
inline ScheduleDescription RenderEffect(EffectPool& pool, std::uint64_t id,
                                        const std::string& relative,
                                        const std::filesystem::path& dxc) {
    const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "assets/effects" / relative;
    SourceOptions options;
    options.preprocessor = dxc;
    std::shared_ptr<const EffectDefinition> definition;
    std::string reason;
    Check(LoadEffectDefinition(path, options, definition, reason), path.u8string() + ":" + reason);
    std::shared_ptr<EffectInstance> instance;
    Check(EffectInstance::Create(definition, pool, instance, reason), reason);
    ScheduleDescription description;
    Check(DescribeSchedule({id, 1, path, instance}, description, reason), reason);
    return description;
}
inline CompiledEffects CompileEffects(ScheduleInput& input, EffectCompiler& compiler) {
    CompiledEffects effects;
    std::string reason;
    for (const auto& e : input.effects) {
        auto& compiled = effects[e.binding.id];
        Check(compiler.Compile(e, compiled, reason), reason);
        for (const auto& pass : compiled->passes)
            Check(bool(pass.program), e.binding.path.u8string() + ":" + pass.diagnostic);
        input.techniqueValidity[e.binding.id] = compiled->techniques;
    }
    input.context = [](auto, int, auto) { return Context(); };
    return effects;
}
inline RenderGeometry Geometry(std::uint64_t object = 3) {
    RenderGeometry geometry;
    ModelReplayResource vertices;
    vertices.key = {1, 1};
    const float data[][8] = {
        {-1, 1, 0, 0, 0, 1, 0, 0}, {3, 1, 0, 0, 0, 1, 2, 0}, {-1, -3, 0, 0, 0, 1, 0, 2}};
    vertices.bytes.resize(sizeof(data));
    std::memcpy(vertices.bytes.data(), data, sizeof(data));
    ModelReplayResource image;
    image.key = {2, 1};
    image.kind = ReplayResourceKind::Texture;
    image.texture.levels = {{1, 1, {255, 255, 255, 255}}};
    geometry.resources = {vertices, image};
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = vertices.key;
    draw.drawVertexCount = 3;
    draw.pipeline.raster.depthTest = draw.pipeline.raster.depthWrite =
        draw.pipeline.raster.alphaBlend = false;
    draw.pipeline.raster.cull = BatchCull::None;
    draw.pipeline.shader.layout.stages[0].role = MaterialTextureRole::Base;
    draw.textures[0] = image.key;
    geometry.draws[{object, 0}] = draw;
    return geometry;
}
inline ScheduleInput NestedInput(EffectPool& pool, const std::filesystem::path& dxc) {
    ScheduleInput input;
    for (const auto& e : std::vector<std::pair<unsigned, std::string>>{{10, "Material.fx"},
                                                                       {11, "Child.fx"},
                                                                       {12, "OuterScene.fx"},
                                                                       {13, "InnerScene.fx"},
                                                                       {14, "ReaderMaterial.fx"}})
        input.effects.push_back(RenderEffect(pool, e.first, "execution/" + e.second, dxc));
    input.objects = {{1, "/models/outer.x", 30, true, true, {0}, true},
                     {2, "/models/inner.x", 20, true, true, {0}, true},
                     {3, "/models/leaf.pmx", 10, false, true, {1}}};
    input.assignments.effects[{0, 1, -1}] = {Assignment::Kind::Effect, 12};
    input.assignments.effects[{0, 2, -1}] = {Assignment::Kind::Effect, 13};
    input.assignments.effects[{0, 3, -1}] = {Assignment::Kind::Effect, 14};
    return input;
}
}
