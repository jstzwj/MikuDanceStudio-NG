#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "portable_material_history_fixture.hpp"
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
    options.backend = DefaultBackend();
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
                if (extra == 1 && !dual && !mode) {
                    const auto revision = session.Revision();
                    const auto updates = session.Runtime().Counters().frames;
                    session.SetMipmaps(false);
                    settings.display = session.DisplayOptions();
                    draw();
                    for (const auto& pass : frame.passes)
                        for (const auto& command : pass.draws)
                            for (unsigned stage = 0; stage < 3; ++stage)
                                if (command.pipeline.shader.layout.stages[stage].role !=
                                    MaterialTextureRole::None) {
                                    const auto& sampler = command.pipeline.shader.samplers[stage];
                                    Require(!sampler.mipmaps &&
                                                sampler.min == SamplerFilter::Linear &&
                                                sampler.mag == SamplerFilter::Linear &&
                                                sampler.maxAnisotropy == 1,
                                            "editor base LOD policy");
                                }
                    session.SetMipmaps(true);
                    settings.display = session.DisplayOptions();
                    Require(draw() == original, "mip toggle restores exact image");
                    Require(session.Revision() == revision &&
                                session.Runtime().Counters().frames == updates,
                            "mip toggle must not edit or evaluate scene");
                }
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
    editor::EditorSession historySession;
    std::string reason;
    Require(MaterialHistoryScene(historySession, root, reason), reason);
    render::SceneRenderSources sources;
    sources.models = historySession.Assets().modelPaths;
    sources.accessories = historySession.Assets().accessoryPaths;
    Require(producer.Load(historySession.Runtime(), sources, assets, reason), reason);
    Require(producer.HasMaterialHistory(), "export warmup required");
    settings.shadow.mode = 1;
    RenderFrame first, stable, repeated;
    Require(producer.Build(historySession.Runtime(), settings, first, reason), reason);
    const auto initial = submit(1, first);
    Require(producer.Build(historySession.Runtime(), settings, repeated, reason), reason);
    Require(initial == submit(1, repeated), "build must not commit material state");
    Require(producer.Commit(), "first inherited frame invalidates static cache");
    Require(!producer.Commit(), "commit is consumed once");
    Require(producer.Build(historySession.Runtime(), settings, stable, reason), reason);
    const auto steady = submit(1, stable);
    Require(steady != initial, "inherited spadd changes the next frame");
    Require(!producer.Commit(), "stable material state retains static cache");
    Require(producer.Load(historySession.Runtime(), sources, assets, reason), reason);
    Require(producer.Build(historySession.Runtime(), settings, repeated, reason), reason);
    Require(submit(1, repeated) == steady, "asset reload preserves live effect state");
    producer.ResetHistory();
    Require(producer.Build(historySession.Runtime(), settings, repeated, reason), reason);
    Require(submit(1, repeated) == initial, "device replacement resets material state");
    auto invalid = settings;
    invalid.width = 0;
    Require(!producer.Build(historySession.Runtime(), invalid, repeated, reason), "invalid frame");
    Require(!producer.Commit(), "failed build cannot commit material state");
    Require(producer.Build(historySession.Runtime(), settings, repeated, reason), reason);
    Require(submit(1, repeated) == initial, "failed build preserves committed state");
    Require(historySession.Edit(
                "accessory-before-model",
                [](auto& d, auto&) {
                    d.physics.accessoryRenderOrder = 1;
                    return true;
                },
                reason),
            reason);
    Require(producer.Build(historySession.Runtime(), settings, repeated, reason), reason);
    Require(submit(1, repeated) == steady, "in-frame material state follows display order");
    Require(!producer.Commit(), "explicit assignment precedes dependent material");
    std::cout << "Portable UV1-4, subtexture, morph, shadow and exact repeated output: " << cases
              << " cases\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
