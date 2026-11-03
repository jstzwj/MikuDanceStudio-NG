#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
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
RenderFrame Frame(unsigned width, unsigned samples, unsigned pipeline, bool alpha) {
    auto f = sampling_fixture::Fixture(1, SamplerFilter::Point, 1, SamplerAddress::Clamp, 1, false,
                                       pipeline == 1);
    f.targets[0].description.width = f.targets[1].description.width = width;
    f.targets.push_back({4, f.targets[0].description});
    f.targets[0].description.samples = f.targets[1].description.samples = samples;
    f.targets[0].description.access = unsigned(TextureAccess::ColorAttachment);
    auto& pass = f.passes.back();
    pass.color->resolve = samples > 1 ? 4 : 0;
    if (samples == 1)
        pass.color->target = 4;
    const sampling_fixture::Vertex vertices[] = {
        {-.73f, -.71f, .5f}, {-.03f, .77f, .5f}, {.79f, -.67f, .5f}};
    f.resources[0].bytes.resize(sizeof vertices);
    std::memcpy(f.resources[0].bytes.data(), vertices, sizeof vertices);
    auto& draw = pass.draws.front();
    draw.drawVertexCount = 3;
    draw.textures = {};
    draw.pipeline.shader.layout = {};
    draw.pipeline.shader.nativeDiffuse =
        draw.pipeline.shader.diffuse = {.25f, .75f, .5f, alpha ? .5f : 1.f};
    draw.pipeline.shadow.color = draw.pipeline.shader.diffuse;
    if (pipeline == 2) {
        ShaderProgramDesc desc;
        desc.vertex.hlsl = desc.pixel.hlsl = R"(
struct V { float4 p:SV_Position; };
V VS(float3 p:POSITION) { V o; o.p=float4(p,1); return o; }
float4 PS(V i):SV_Target { return float4(.25,.75,.5,ALPHA); }
)";
        desc.pixel.hlsl.replace(desc.pixel.hlsl.find("ALPHA"), 5, alpha ? ".5" : "1");
        desc.vertex.hlsl = desc.pixel.hlsl;
        desc.vertex.entry = "VS";
        desc.pixel.entry = "PS";
        desc.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3}};
        draw.shader = ReplayShader{};
        draw.shader->pipeline.program = std::make_shared<const ShaderProgram>(std::move(desc));
        draw.shader->pipeline.raster = draw.pipeline.raster;
    }
    CHECK(AppendPostProcess(f, 4, 7, width, 64));
    f.finalColor = 7;
    return f;
}
AttachmentReadback Render(Device& device, const RenderFrame& frame, unsigned read = 7) {
    auto valid = ValidateRenderFrame(frame);
    if (!valid)
        throw std::runtime_error(valid.reason);
    SceneRenderer renderer(device);
    PreparedModelFrame prepared;
    CompletionToken token;
    AttachmentReadback image, repeat, resolved;
    auto preparedResult = renderer.Prepare(1, frame, prepared);
    if (!preparedResult)
        throw std::runtime_error(preparedResult.reason);
    CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, read, image));
    CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, read, repeat));
    CHECK(image.bytes == repeat.bytes);
    if (read == 7) {
        CHECK(renderer.ReadTarget(1, 4, resolved) && resolved.bytes == image.bytes);
        if (frame.targets[0].description.samples > 1)
            CHECK(!renderer.ReadTarget(1, 1, resolved));
    }
    CHECK(renderer.Unload(1) && renderer.Detach());
    return image;
}
float Channel(const AttachmentReadback& image, unsigned n) {
    std::uint16_t h;
    std::memcpy(&h, image.bytes.data() + n * 2, 2);
    return HalfToFloat(h);
}
void Hdr(Device& device, unsigned width, unsigned samples) {
    auto f = Frame(width, samples, 2, true);
    f.passes.pop_back();
    f.finalColor = 0;
    f.targets.pop_back();
    for (auto id : {1u, 4u})
        for (auto& t : f.targets)
            if (t.id == id)
                t.description.format = TextureFormat::Rgba16Float;
    auto second = f.targets[0];
    second.id = 5;
    auto resolved = f.targets[3];
    resolved.id = 6;
    f.targets.push_back(second);
    f.targets.push_back(resolved);
    auto& pass = f.passes.back();
    pass.colors = {*pass.color, *pass.color};
    pass.color.reset();
    pass.colors[0].clear = {-2, 4, .5f, .25f};
    pass.colors[1].target = 5;
    pass.colors[1].resolve = 6;
    pass.colors[1].clear = {8, -.5f, 2, 1};
    auto& draw = pass.draws.front();
    auto desc = draw.shader->pipeline.program->description;
    desc.vertex.hlsl = desc.pixel.hlsl = R"(
struct V { float4 p:SV_Position; };
V VS(float3 p:POSITION) { V o; o.p=float4(p,1); return o; }
struct O { float4 a:SV_Target0; float4 b:SV_Target1; };
O PS(V i) { O o; o.a=float4(3,-1,.25,.5); o.b=float4(-4,2,1,.75); return o; }
)";
    desc.colorOutputs = {4, 4};
    draw.shader->pipeline.program = std::make_shared<const ShaderProgram>(std::move(desc));
    draw.shader->pipeline.colorFormats = {TextureFormat::Rgba16Float, TextureFormat::Rgba16Float};
    for (auto id : {4u, 6u}) {
        const auto image = Render(device, f, id);
        const float clearA[] = {-2, 4, .5f, .25f}, clearB[] = {8, -.5f, 2, 1};
        const float centerA[] = {3, -1, .25f, .5f}, centerB[] = {-4, 2, 1, .75f};
        for (unsigned c = 0; c < 4; ++c) {
            CHECK(Channel(image, c) == (id == 4 ? clearA[c] : clearB[c]));
            CHECK(Channel(image, (32 * width + width / 2) * 4 + c) ==
                  (id == 4 ? centerA[c] : centerB[c]));
        }
    }
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
        auto created = CreateOffscreenBackendDevice(width, 64, options);
        CHECK(created);
        auto& device = *created.device;
        const auto mask = device.FormatFeatures(TextureFormat::Rgba8).sampleCounts &
                          device.FormatFeatures(TextureFormat::DepthStencil).sampleCounts;
        CHECK(mask & 14u);
        for (unsigned samples : {2u, 4u, 8u}) {
            if (!(mask & samples))
                continue;
            for (unsigned pipeline = 0; pipeline < 3; ++pipeline)
                for (bool alpha : {false, true}) {
                    const auto single = Render(device, Frame(width, 1, pipeline, alpha));
                    auto frame = Frame(width, samples, pipeline, alpha);
                    const auto multi = Render(device, frame);
                    auto transformed = Frame(width, 1, pipeline, alpha);
                    CHECK(ApplyFrameMultisampling(transformed, samples));
                    CHECK(Render(device, transformed).bytes == multi.bytes);
                    unsigned partial = 0;
                    const auto center = (32 * width + width / 2) * 4;
                    for (unsigned c = 0; c < 4; ++c)
                        CHECK(single.bytes[center + c] == multi.bytes[center + c]);
                    for (unsigned n = 0; n < multi.bytes.size(); n += 4)
                        partial += multi.bytes[n] != 255 && multi.bytes[n] != multi.bytes[center];
                    CHECK(partial > 20);
                    auto continued = frame;
                    auto load = continued.passes[1];
                    load.color->load = load.depthStencil->load = AttachmentLoad::Load;
                    load.draws.clear();
                    continued.passes.insert(continued.passes.begin() + 2, load);
                    CHECK(Render(device, continued).bytes == multi.bytes);
                    frame.passes[1].color->store = AttachmentStore::Discard;
                    CHECK(Render(device, frame).bytes == multi.bytes);
                    CHECK(Render(device, Frame(width, 1, pipeline, alpha)).bytes == single.bytes);
                    ++cases;
                }
            if (device.FormatFeatures(TextureFormat::Rgba16Float).sampleCounts & samples)
                Hdr(device, width, samples);
        }
    }
    std::printf(
        "PASS %s: %u MSAA cases, builtin/shadow/generic, partial coverage, alpha, strict post-copy/repeat/restoration, MRT/HDR, resize and device replacement\n",
        api.c_str(), cases);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
