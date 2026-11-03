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
RenderFrame Frame(unsigned width, unsigned pipeline, BatchCull cull, bool blend) {
    auto f = sampling_fixture::Fixture(1, SamplerFilter::Point, 1, SamplerAddress::Clamp, 1, false,
                                       pipeline == 1);
    f.finalColor = 1;
    f.targets[0].description.width = f.targets[1].description.width = width;
    const sampling_fixture::Vertex vertices[] = {
        {-.75f, -.75f, .5f}, {0, .75f, .5f}, {.75f, -.75f, .5f}};
    auto& vb = f.resources[0].bytes;
    vb.resize(sizeof vertices);
    std::memcpy(vb.data(), vertices, sizeof vertices);
    auto& draw = f.passes.back().draws.front();
    draw.drawVertexCount = 3;
    draw.textures = {};
    draw.pipeline.shader.layout = {};
    draw.pipeline.shader.diffuse =
        draw.pipeline.shader.nativeDiffuse = {.25f, .75f, .5f, blend ? .5f : 1.f};
    draw.pipeline.shadow.color = draw.pipeline.shader.diffuse;
    draw.pipeline.raster.cull = cull;
    draw.pipeline.raster.alphaBlend = blend;
    if (pipeline == 2) {
        const auto raster = draw.pipeline.raster;
        draw.pipeline = {};
        ShaderProgramDesc desc;
        desc.vertex.hlsl = desc.pixel.hlsl = R"(
struct V { float4 p:SV_Position; };
V VS(float3 p:POSITION) { V v; v.p=float4(p,1); return v; }
float4 PS(V v):SV_Target { return float4(.25,.75,.5,ALPHA); }
)";
        auto& source = desc.pixel.hlsl;
        source.replace(source.find("ALPHA"), 5, blend ? ".5" : "1");
        desc.vertex.hlsl = source;
        desc.vertex.entry = "VS";
        desc.pixel.entry = "PS";
        desc.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3}};
        draw.shader = ReplayShader{};
        draw.shader->pipeline.program = std::make_shared<const ShaderProgram>(std::move(desc));
        draw.shader->pipeline.raster = raster;
    }
    return f;
}
AttachmentReadback Render(Device& d, const RenderFrame& f) {
    const auto valid = ValidateRenderFrame(f);
    CHECK(valid);
    SceneRenderer renderer(d);
    PreparedModelFrame prepared;
    CompletionToken token;
    AttachmentReadback image, repeated;
    auto result = renderer.Prepare(1, f, prepared);
    if (!result)
        throw std::runtime_error(result.reason);
    CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, f.finalColor, image));
    CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, f.finalColor, repeated));
    CHECK(image.bytes == repeated.bytes);
    CHECK(renderer.Unload(1) && renderer.Detach());
    return image;
}
std::size_t Coverage(const AttachmentReadback& image) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.bytes.size(); i += 4)
        count += image.bytes[i] != 255 || image.bytes[i + 1] != 255 || image.bytes[i + 2] != 255;
    return count;
}
void CheckLineCoverage(Device& device, unsigned width) {
    auto frame=Frame(width,2,BatchCull::None,false);
    auto& draw=frame.passes.back().draws.front();
    draw.pipeline.topology=draw.shader->pipeline.topology=PrimitiveTopology::LineList;
    draw.drawVertexCount=2;
    const auto vertex = [&](float x, float y) {
        return sampling_fixture::Vertex{x * 2 / width - 1, 1 - y * 2 / 64, .5f};
    };
    const sampling_fixture::Vertex vertices[] = {vertex(8.5f, 8.5f), vertex(58.5f, 28.5f)};
    auto& bytes = frame.resources[0].bytes;
    bytes.resize(sizeof vertices);
    std::memcpy(bytes.data(), vertices, sizeof vertices);
    const auto image = Render(device, frame);
    // Diamond-exit rasterization, slope 2/5 with no halfway ties, includes
    // the first endpoint and excludes the last. Rectangle-based Vulkan lines
    // cover extra pixels even though polygon wireframe uses Bresenham.
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const bool expected = x >= 8 && x < 58 && y == 8 + (2 * (x - 8) + 2) / 5;
            const auto at = (y * width + x) * 4;
            const bool covered =
                image.bytes[at] != 255 || image.bytes[at + 1] != 255 || image.bytes[at + 2] != 255;
            if (covered != expected)
                std::fprintf(stderr, "line width=%u pixel=%u,%u covered=%u expected=%u\n", width, x,
                             y, covered, expected);
            CHECK(covered == expected);
        }
}
void CheckUniformAlpha(Device& device, unsigned width) {
    for (bool lighting : {false, true}) {
        auto frame = Frame(width, 0, BatchCull::None, false);
        auto& shader = frame.passes.back().draws.front().pipeline.shader;
        shader.nativeDiffuse[3] = .7f;
        shader.lighting = lighting;
        shader.globalAmbient = {1, 1, 1, 1};
        shader.materialAmbient = {.25f, .75f, .5f, .7f};
        // Vary homogeneous W to exercise perspective interpolation of a
        // material constant exactly at a sensitive UNORM conversion boundary.
        shader.projection[11] = .25f;
        const sampling_fixture::Vertex vertices[] = {
            {-.75f, -.75f, .2f}, {0, .75f, .4f}, {.75f, -.75f, .8f}};
        auto& bytes = frame.resources[0].bytes;
        bytes.resize(sizeof vertices);
        std::memcpy(bytes.data(), vertices, sizeof vertices);
        const auto image = Render(device, frame);
        unsigned covered = 0;
        for (std::size_t at = 0; at < image.bytes.size(); at += 4)
            if (image.bytes[at] != 255 || image.bytes[at + 1] != 255 ||
                image.bytes[at + 2] != 255) {
                CHECK(image.bytes[at + 3] == 178);
                ++covered;
            }
        CHECK(covered > 1000);
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
        auto device = CreateOffscreenBackendDevice(width, 64, options);
        CHECK(device && device.device->RasterFeatures().wireframe);
        CheckLineCoverage(*device.device,width);
        CheckUniformAlpha(*device.device, width);
        for (unsigned pipeline = 0; pipeline < 3; ++pipeline)
            for (auto cull : {BatchCull::None, BatchCull::Clockwise, BatchCull::CounterClockwise})
                for (bool blend : {false, true}) {
                    auto frame = Frame(width, pipeline, cull, blend);
                    const auto solid = Render(*device.device, frame);
                    auto& draw = frame.passes.back().draws.front();
                    auto& raster =
                        draw.shader ? draw.shader->pipeline.raster : draw.pipeline.raster;
                    raster.wireframe = true;
                    const auto lines = Render(*device.device, frame);
                    const auto filled = Coverage(solid), edges = Coverage(lines);
                    CHECK(filled ? edges > 0 && edges * 3 < filled : edges == 0);
                    if (filled) {
                        const auto center = (32 * width + width / 2) * 4;
                        CHECK(solid.bytes[center] != 255 && lines.bytes[center] == 255 &&
                              lines.bytes[center + 1] == 255 && lines.bytes[center + 2] == 255 &&
                              lines.bytes[center + 3] == 255);
                    }
                    raster.wireframe = false;
                    CHECK(Render(*device.device, frame).bytes == solid.bytes);
                    raster.wireframe = true;
                    CHECK(Render(*device.device, frame).bytes == lines.bytes);
                    ++cases;
                }
    }
    std::printf(
        "PASS %s: %u wireframe cases, builtin/shadow/generic, culling, alpha, exact restoration, repeat, resize and device recreation\n",
        api.c_str(), cases);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
