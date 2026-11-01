#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
namespace {
void Require(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
}
int main(int argc, char** argv) try {
    Require(argc == 3, "arguments");
    const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    FileAssetSource assets(std::filesystem::absolute(std::filesystem::u8path(argv[2])));
    BackendOptions options;
    options.backend = Backend::Vulkan;
    if (const auto* backend = std::getenv("MDS_GPU_BACKEND")) {
        if (std::string(backend) == "d3d11")
            options.backend = Backend::D3D11;
        if (std::string(backend) == "d3d12")
            options.backend = Backend::D3D12;
    }
    if (const auto* driver = std::getenv("MDS_GPU_DRIVER"))
        if (std::string(driver) == "software")
            options.driver = BackendDriver::Software;
    if (const auto* library = std::getenv("MDS_TEST_VULKAN_LIBRARY"))
        options.vulkanLibrary = library;
    auto device = CreateOffscreenBackendDevice(128, 96, options);
    Require(bool(device), device.reason);
    SceneRenderer renderer(*device.device);
    render::SceneFrameProducer producer;
    render::SceneRenderSettings settings;
    settings.width = 128;
    settings.height = 96;
    settings.shadow.mapSize = 32;
    const auto submit = [&](std::uint64_t sceneId, const RenderFrame& value) {
        PreparedModelFrame prepared;
        CompletionToken completion;
        auto result = renderer.Prepare(sceneId, value, prepared);
        Require(bool(result), result.reason);
        result = renderer.Submit(prepared, completion);
        Require(bool(result), result.reason);
        AttachmentReadback image, repeated;
        Require(bool(renderer.ReadTarget(sceneId, value.finalColor, image)), "read");
        Require(bool(renderer.Submit(prepared, completion)), "repeat submit");
        Require(bool(renderer.ReadTarget(sceneId, value.finalColor, repeated)), "repeat read");
        Require(image.bytes == repeated.bytes, "repeat pixels");
        return image.bytes;
    };
    unsigned cases = 0;
    for (unsigned extra = 1; extra <= 4; ++extra)
        for (bool dual : {false, true}) {
            editor::EditorSession session;
            std::string reason;
            const auto name = "uv" + std::to_string(extra) + (dual ? "-dual.pmx" : "-only.pmx");
            Require(session.ImportModel(root / name, reason), reason);
            Require(session.Edit(
                        "view",
                        [](auto& d, auto&) {
                            d.view.groundGrid = d.view.groundShadow = 0;
                            return true;
                        },
                        reason),
                    reason);
            render::SceneRenderSources sources;
            sources.models = session.Assets().modelPaths;
            Require(producer.Load(session.Runtime(), sources, assets, reason), reason);
            for (unsigned mode : {0u, 1u, 2u}) {
                settings.shadow.mode = mode;
                RenderFrame frame;
                ModelReplayResource saved;
                const auto draw = [&]() {
                    Require(producer.Build(session.Runtime(), settings, frame, reason), reason);
                    const auto& main = frame.passes[mode ? 1 : 0].draws.front();
                    Require(AdditionalVertexUvs(main.pipeline.vertexLayout) == extra, "layout");
                    for (const auto& resource : frame.resources)
                        if (resource.key.id == main.vertices.id)
                            saved = resource;
                    Require(saved.bytes.size() == 3 * (32 + extra * 16), "vertex bytes");
                    return submit(1, frame);
                };
                const auto original = draw();
                const auto originalGeometry = saved;
                if (!mode) {
                    auto transformed = frame;
                    for (auto& pass : transformed.passes)
                        for (auto& command : pass.draws)
                            for (unsigned i = 0; i < 3; ++i)
                                if (command.pipeline.shader.layout.stages[i].coordinates ==
                                    MaterialCoordinates::MeshUv1)
                                    command.pipeline.shader.textureTransforms[i] = {
                                        0,     1,     0, 0, -1,   0,   0, 0,
                                        .125f, -.25f, 1, 0, .25f, .5f, 0, 1};
                    const auto transformedPixels = submit(2, transformed);
                    auto reference = frame;
                    for (auto& resource : reference.resources)
                        if (resource.key.id == originalGeometry.key.id) {
                            auto* bytes = resource.bytes.data();
                            for (unsigned vertex = 0; vertex < 3; ++vertex) {
                                auto* input = bytes + vertex * (32 + extra * 16) + 32;
                                float uv[4];
                                std::memcpy(uv, input, sizeof(uv));
                                const float xy[]{-uv[1] + uv[2] * .125f + uv[3] * .25f,
                                                 uv[0] - uv[2] * .25f + uv[3] * .5f};
                                std::memcpy(input, xy, sizeof(xy));
                            }
                            resource.bytes.Seal();
                        }
                    Require(transformedPixels == submit(3, reference),
                            "UV1 float4 row-vector matrix");
                    Require(transformedPixels != original, "matrix must affect pixels");
                }
                Require(session.SetMorph(0, 3, .75f, reason), reason);
                const auto shifted = draw();
                Require(original != shifted, "UV morph must change pixels");
                const auto* pose = session.Runtime().Snapshot(0);
                for (unsigned v = 0; v < 3; ++v)
                    for (unsigned u = 0; u < extra; ++u)
                        Require(
                            std::memcmp(saved.bytes.data() + v * (32 + 16 * extra) + 32 + 16 * u,
                                        pose->vertices[v].additionalUv[u].data(), 16) == 0,
                            "every UV component must survive publication");
                Require(saved.key.version > originalGeometry.key.version, "UV morph version");
                Require(session.SetMorph(0, 3, 0, reason), reason);
                Require(draw() == original, "reset UV restores exact image");
                ++cases;
            }
        }
    std::cout << "Portable UV1-4, subtexture, morph, shadow and exact repeated output: " << cases
              << " cases\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
