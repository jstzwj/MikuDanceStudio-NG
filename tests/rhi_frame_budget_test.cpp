#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace mikudancestudio::rhi;
void Check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
RenderFrame Frame(unsigned draws) {
    RenderFrame frame;
    frame.finalColor = 1;
    frame.targets = {
        {1,
         {8, 8, TextureFormat::Rgba8, TextureAccess::ColorAttachment | TextureAccess::CopySource}}};
    const float vertices[][8]{
        {-1, -1, .5f, 0, 0, -1, 0, 0}, {3, -1, .5f, 0, 0, -1, 0, 0}, {-1, 3, .5f, 0, 0, -1, 0, 0}};
    ModelReplayResource resource;
    resource.key = {1, 1};
    resource.bytes.resize(sizeof(vertices));
    std::memcpy(resource.bytes.data(), vertices, sizeof(vertices));
    frame.resources = {resource};
    FramePass pass;
    pass.color = FrameAttachment{1};
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = resource.key;
    draw.drawVertexCount = 3;
    draw.pipeline.raster.cull = BatchCull::None;
    draw.pipeline.raster.depthTest = draw.pipeline.raster.depthWrite = false;
    draw.pipeline.raster.alphaBlend = false;
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto& shader = draw.pipeline.shader;
    shader.world = shader.view = shader.projection = shader.normalView = identity;
    shader.nativeDiffuse = shader.diffuse = {.25f, .5f, .75f, 1};
    pass.draws.assign(draws, draw);
    frame.passes = {std::move(pass)};
    return frame;
}
int main(int argc, char** argv) try {
    BackendOptions options;
    options.backend = DefaultBackend();
    if (argc > 1) {
        const std::string name = argv[1];
        options.backend = name == "d3d11"   ? Backend::D3D11
                          : name == "d3d12" ? Backend::D3D12
                          : name == "metal" ? Backend::Metal
                                            : Backend::Vulkan;
    }
    auto native = CreateOffscreenBackendDevice(8, 8, options);
    Check(bool(native), native.reason);
    auto& device = *native.device;
    SceneRenderer renderer(device);
    PreparedModelFrame large, small;
    const auto render = [&](const PreparedModelFrame& prepared, std::uint64_t scene) {
        CompletionToken token;
        const auto submitted = renderer.Submit(prepared, token);
        Check(bool(submitted), submitted.reason);
        AttachmentReadback pixels;
        Check(renderer.ReadTarget(scene, 1, pixels), "frame readback");
        return pixels.bytes;
    };
    auto result = renderer.Prepare(1, Frame(1), small);
    Check(bool(result), result.reason);
    const auto expected = render(small, 1);
    // The legacy arenas held 512 draws / 2 MiB. Reserve both independently,
    // retain a larger prepared scene while preparing a different smaller one,
    // and cycle all GPU frame slots repeatedly without replacing pending work.
    Check(device.ReserveFrameResources(1024, 4 * 1024 * 1024), "reserve larger frame arenas");
    result = renderer.Prepare(1, Frame(1024), large);
    Check(bool(result), result.reason);
    result = renderer.Prepare(2, Frame(1), small);
    Check(bool(result), result.reason);
    for (unsigned i = 0; i < 8; ++i) {
        Check(render(large, 1) == expected, "large-frame exact pixels");
        Check(render(small, 2) == expected, "small-frame exact pixels");
    }
    // DX11 streams draws and does not advertise a finite per-frame arena.
    if (device.ModelFeatures().maxDraws < UINT32_MAX ||
        device.ShaderFeatures().maxFrameConstantBytes)
        Check(!device.ReserveFrameResources(UINT32_MAX, UINT64_MAX),
              "reject impossible frame budget");
    Check(render(large, 1) == expected, "failed reservation preserves prepared frames");
    Check(renderer.Unload(1) && renderer.Unload(2), "release expanded frames");
    std::printf("%s: 1024 draws, arena growth, all frame slots and exact repeats passed\n",
                native.adapter.c_str());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
