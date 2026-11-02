#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "material_sampling_fixture.hpp"
#include "rhi_test_platform.hpp"
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
namespace {
AttachmentReadback Render(Device& device, RenderFrame frame) {
    frame.finalColor = 1;
    const auto valid = ValidateRenderFrame(frame);
    if (!valid)
        throw std::runtime_error(valid.reason);
    SceneRenderer renderer(device);
    PreparedModelFrame prepared;
    CompletionToken token;
    AttachmentReadback image, repeated;
    CHECK(renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, 1, image));
    CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, 1, repeated));
    CHECK(image.bytes == repeated.bytes);
    CHECK(renderer.Unload(1) && renderer.Detach());
    return image;
}
void DisableMips(RenderFrame& frame) {
    for (auto& pass : frame.passes)
        for (auto& draw : pass.draws) {
            for (auto& sampler : draw.pipeline.shader.samplers)
                sampler.mipmaps = false;
            if (draw.shader)
                for (auto& sampler : draw.shader->samplers)
                    sampler.sampler.mipmaps = false;
        }
}
void Generic(RenderFrame& frame, bool explicitLod) {
    auto& draw = frame.passes.back().draws.front();
    const auto sampler = draw.pipeline.shader.samplers[0];
    draw.pipeline = {};
    draw.textures = {};
    ShaderProgramDesc description;
    description.vertex.hlsl = description.pixel.hlsl = R"(
Texture2D image : register(t0); SamplerState sampler0 : register(s0);
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V VS(float3 p:POSITION, float2 uv:TEXCOORD0) { V v; v.p=float4(p,1); v.uv=uv; return v; }
float4 implicitLod(V v):SV_Target { return image.Sample(sampler0,v.uv); }
float4 explicitLod(V v):SV_Target { return image.SampleLevel(sampler0,v.uv,2); }
)";
    description.vertex.entry = "VS";
    description.pixel.entry = explicitLod ? "explicitLod" : "implicitLod";
    description.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                          {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
    description.bindings = {{ShaderBindingKind::Texture, 0}, {ShaderBindingKind::Sampler, 0}};
    draw.shader = ReplayShader{};
    draw.shader->pipeline.program = std::make_shared<const ShaderProgram>(description);
    auto& raster = draw.shader->pipeline.raster;
    raster.depthTest = raster.depthWrite = raster.alphaBlend = false;
    raster.cull = BatchCull::None;
    draw.shader->textures = {{0, {2, 1}, {}}};
    draw.shader->samplers = {{0, sampler}};
}
}
int main(int argc, char** argv) try {
    CHECK(argc == 2);
    const std::string api = argv[1];
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    ConfigureVulkanTest(options, api);
    unsigned cases = 0;
    for (unsigned width : {96u, 127u}) {
        // Fresh devices exercise sampler recreation as well as the live cache.
        auto device = CreateOffscreenBackendDevice(width, 64, options);
        CHECK(device);
        for (auto filter : {SamplerFilter::Point, SamplerFilter::Linear})
            for (auto address : {SamplerAddress::Wrap, SamplerAddress::Clamp})
                for (unsigned pipeline = 0; pipeline < 4; ++pipeline) {
                    auto frame = sampling_fixture::Fixture(1, filter, 1, address, 37.37f, true,
                                                           pipeline == 1);
                    frame.targets[0].description.width = frame.targets[1].description.width = width;
                    if (pipeline >= 2)
                        Generic(frame, pipeline == 3);
                    const auto enabled = Render(*device.device, frame);
                    auto disabled = frame;
                    DisableMips(disabled);
                    const auto clamped = Render(*device.device, disabled);
                    auto baseOnly = frame;
                    baseOnly.resources[1].texture.levels.resize(1);
                    const auto base = Render(*device.device, baseOnly);
                    CHECK(enabled.bytes != base.bytes);
                    CHECK(clamped.bytes == base.bytes);
                    // Cache identity must include the flag in both directions.
                    CHECK(Render(*device.device, frame).bytes == enabled.bytes);
                    CHECK(Render(*device.device, disabled).bytes == base.bytes);
                    const auto pixel = (32 * width + width / 2) * 4;
                    CHECK(base.bytes[pixel] == 0 && base.bytes[pixel + 1] == 255 &&
                          base.bytes[pixel + 2] == 0 && base.bytes[pixel + 3] == 255);
                    ++cases;
                }
    }
    std::printf(
        "PASS %s: %u base-LOD cases, builtin/shadow/generic implicit/explicit, exact alpha, repeat, resize and device recreation\n",
        api.c_str(), cases);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
