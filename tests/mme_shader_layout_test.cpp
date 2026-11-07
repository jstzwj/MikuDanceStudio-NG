#include "mme_render_fixture.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
using namespace mme_test;
int main(int argc, char** argv) try {
    Check(argc == 2, "usage: mme_shader_layout_test dxc");
    EffectPool pool;
    FrameClock clock;
    TargetRegistry targets;
    std::string reason;
    auto compilerTool = CreateDxcSpirvCompiler({argv[1]});
    Check(compilerTool->Available(), compilerTool->Identity());
    EffectCompiler compiler({compilerTool, "null"});
    auto effect = RenderEffect(pool, 10, "execution/LayoutMaterial.fx", argv[1]);
    ScheduleInput input;
    input.effects = {effect};
    input.objects = {{3, "/leaf.pmx", 0, false, true, {1}}};
    input.assignments.effects[{0, 3, -1}] = {Assignment::Kind::Effect, 10};
    auto compiled = CompileEffects(input, compiler);
    const auto& layout = compiled.at(10)->layout;
    const std::vector<unsigned> offsets = {0, 4, 16, 32, 36, 40, 48, 80, 144};
    Check(layout.size() == offsets.size(), "constant count excludes static/private values");
    for (unsigned i = 0; i < offsets.size(); ++i)
        Check(layout[i].offset == offsets[i], "FX packing offset");
    Check(compiled.at(10)->constantBytes == 160, "rounded constant block");
    Check(compiled.at(10)->passes[0].samplers.size() == 16, "all sixteen sampler slots live");
    const auto compilations = compilerTool->Stats().compilations;
    std::shared_ptr<const CompiledEffect> again;
    Check(compiler.Compile(effect, again, reason) && compiler.Stats().cacheHits == 1, reason);
    Check(compilerTool->Stats().compilations == compilations, "cache skips actual compiler");
    auto changed = effect;
    changed.binding.revision++;
    Check(compiler.Compile(changed, again, reason) && compiler.Stats().effects == 2,
          "reload revision invalidates effect cache");
    auto wrong = compiled.at(10)->passes[0].program->description;
    wrong.bindings[1].members[0].offset = 4;
    Check(!ValidateSpirvProgram(wrong), "reject wrong constant offset before device");
    wrong = compiled.at(10)->passes[0].program->description;
    wrong.pixel.spirv.resize(12);
    Check(!ValidateSpirvProgram(wrong), "reject truncated module");
    Check(targets.Reconcile(input.effects, 64, 48, 1, reason), reason);
    std::unique_ptr<ScheduledFrame> scheduled;
    Check(ScheduledFrame::Prepare(input, targets, pool, clock, 1, 0, 0, true, scheduled, reason),
          reason);
    RenderFrame frame;
    Check(AssembleRenderFrame(*scheduled, compiled, targets, Geometry(), frame, reason), reason);
    Check(std::none_of(frame.targets.begin(), frame.targets.end(),
                       [](const auto& t) { return IsDepthStencil(t.description.format); }),
          "clear-only depth resources are not allocated without a consumer");
    const auto& portableSampler = frame.passes.back().draws[0].shader->samplers[0].sampler;
    Check(portableSampler.min == SamplerFilter::Linear &&
              portableSampler.mag == SamplerFilter::Linear &&
              portableSampler.mip == SamplerFilter::Linear && portableSampler.maxAnisotropy == 1,
          "native anisotropy request uses the portable trilinear profile");
    auto device = CreateNullDevice();
    auto target = device->CreateRenderTexture(frame.targets[0].description);
    auto image = device->CreateTexture({1, 1});
    const std::uint8_t white[] = {255, 255, 255, 255};
    Check(device->UploadTexture(image, {white, 4, 4}), "Null texture upload");
    const auto geometry = Geometry();
    auto vertices = device->CreateBuffer({BufferUsage::Vertex, geometry.resources[0].bytes.size()},
                                         geometry.resources[0].bytes.data());
    const auto& shader = *frame.passes.back().draws[0].shader;
    RenderPassDesc pass;
    pass.color = PassAttachment{target};
    Check(device->BeginFrame({}) && device->BeginRenderPass(pass), "Null begin assembled draw");
    Check(device->SetShaderPipeline(shader.pipeline), device->ShaderDiagnostic());
    ShaderResourceBindings bindings;
    bindings.constants = shader.constants;
    bindings.samplers = shader.samplers;
    for (const auto& texture : shader.textures)
        bindings.textures.push_back({texture.slot, image});
    Check(device->BindShaderResources(bindings), device->ShaderDiagnostic());
    NonIndexedDraw draw{vertices, 0, 3};
    draw.layout = VertexLayout::PositionNormalUv;
    Check(device->Draw(device->Record(draw)), "Null executes assembled draw");
    bindings.textures.pop_back();
    Check(!device->BindShaderResources(bindings), "Null rejects missing slot fifteen");
    Check(device->EndRenderPass() && device->EndFrame(), "Null end");
    auto missing = Geometry();
    missing.resources.pop_back();
    auto retained = frame;
    Check(!AssembleRenderFrame(*scheduled, compiled, targets, missing, frame, reason),
          "reject missing immutable texture");
    Check(frame.passes.size() == retained.passes.size(), "assembly failure preserves output");
    Check(effect.binding.instance->Write("host", Float(.75f), reason), reason);
    Check(!scheduled->Ready(reason), "host write invalidates uncommitted snapshot");
    // Compiler failures are frozen and cached; a new immutable source recovers.
    auto broken =
        std::make_shared<EffectDefinition>(*effect.binding.instance->DefinitionSnapshot());
    auto text = broken->source.expanded;
    const auto at = text.find("return (");
    Check(at != std::string::npos, "fixture return");
    text.insert(at + 7, "unknownFunction() + ");
    broken->source.expanded = text;
    Check(MmeLegacyDescribe(text, broken->metadata, reason), reason);
    std::shared_ptr<EffectInstance> badInstance;
    Check(EffectInstance::Create(broken, pool, badInstance, reason), reason);
    changed.binding = {11, 1, "bad.fx", badInstance};
    Check(DescribeSchedule(changed.binding, changed, reason), reason);
    Check(compiler.Compile(changed, again, reason) && !again->techniques[0],
          "invalid shader is not a valid technique");
    const auto failures = compiler.Stats().failedPasses;
    Check(compiler.Compile(changed, again, reason) && compiler.Stats().failedPasses == failures,
          "failed pass cache");
    Check(compiler.Compile(effect, again, reason) && again->techniques[0], "valid source recovers");
    auto frozen =
        std::make_shared<EffectDefinition>(*effect.binding.instance->DefinitionSnapshot());
    const auto changeSource = [&] {
        std::shared_ptr<EffectInstance> instance;
        Check(EffectInstance::Create(frozen, pool, instance, reason), reason);
        ScheduleDescription description;
        Check(DescribeSchedule({20, 1, "cache.fx", instance}, description, reason), reason);
        const auto effects = compiler.Stats().effects;
        Check(compiler.Compile(description, again, reason) &&
                  compiler.Stats().effects == effects + 1,
              "frozen dependency change invalidates effect cache");
        Check(compiler.Compile(description, again, reason), reason);
    };
    frozen->source.macros["UNUSED_MACRO"] = "1";
    changeSource();
    frozen = std::make_shared<EffectDefinition>(*frozen);
    frozen->source.files.push_back(
        {"inactive-include.fx", "utf-8", "// inactive include dependency"});
    changeSource();
    frozen = std::make_shared<EffectDefinition>(*frozen);
    frozen->source.files.back().utf8 += " modified";
    changeSource();
    EffectCompiler uncached({compilerTool, "null", 0});
    Check(uncached.Compile(effect, again, reason) && uncached.Compile(effect, again, reason) &&
              uncached.Stats().effects == 2 && !uncached.Stats().cacheHits,
          "explicitly disabled cache");
    compiler.Clear();
    const auto effects = compiler.Stats().effects;
    Check(compiler.Compile(effect, again, reason) && compiler.Stats().effects == effects + 1,
          "cache clear/reload");
    // Dead-store removal must never substitute a requested format or discard a
    // surface that an enabled depth test/write can observe.
    auto depthDefinition =
        std::make_shared<EffectDefinition>(*effect.binding.instance->DefinitionSnapshot());
    auto& depthSource = depthDefinition->source.expanded;
    for (const auto& state : std::vector<std::string>{"ZEnable", "ZWriteEnable"}) {
        auto pos = depthSource.find(state + "=false");
        // DXC preprocessing may insert spaces around tokens.
        if (pos == std::string::npos)
            pos = depthSource.find(state + " = false");
        Check(pos != std::string::npos, "fixture depth state");
        pos = depthSource.find("false", pos);
        depthSource.replace(pos, 5, "true");
    }
    Check(MmeLegacyDescribe(depthSource, depthDefinition->metadata, reason), reason);
    std::shared_ptr<EffectInstance> depthInstance;
    Check(EffectInstance::Create(depthDefinition, pool, depthInstance, reason), reason);
    ScheduleDescription depthDescription;
    Check(DescribeSchedule({10, 2, "depth.fx", depthInstance}, depthDescription, reason), reason);
    input.effects = {depthDescription};
    compiled = CompileEffects(input, compiler);
    Check(targets.Reconcile(input.effects, 64, 48, 2, reason), reason);
    Check(ScheduledFrame::Prepare(input, targets, pool, clock, 2, 1, 0, true, scheduled, reason),
          reason);
    Check(AssembleRenderFrame(*scheduled, compiled, targets, Geometry(), frame, reason), reason);
    Check(std::any_of(
              frame.targets.begin(), frame.targets.end(),
              [](const auto& t) { return t.description.format == TextureFormat::Depth24Stencil8; }),
          "live depth retains its exact declared format");
    Check(device->Destroy(target) && device->Destroy(image) && device->Destroy(vertices), "unload");
    std::cout
        << "P5.4 layout, SPIR-V reflection, 16 slots, Null assembly, failure cache and atomicity passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
