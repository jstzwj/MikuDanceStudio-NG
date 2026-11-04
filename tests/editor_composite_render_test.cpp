#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "self_shadow_compare.hpp"
#include "composite_visual_compare.hpp"
#include "../src/render/rhi_model_constants.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace mikudancestudio;
void Require(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
int main(int argc, char** argv) try {
    Require(argc == 3 || argc == 4, "arguments: public fixture directory, assets [dump directory]");
    const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    rhi::FileAssetSource assets(std::filesystem::absolute(std::filesystem::u8path(argv[2])));
    editor::EditorSession session;
    std::string reason;
    Require(session.ImportModel(root / "scene.pmx", reason), reason);
    for (unsigned i = 0; i < 2; ++i) {
        Require(session.ImportAccessory(root / "composite.x", reason), reason);
        auto state = session.Document().accessories[i].state;
        state.position = {i ? 1.f : -2.f, 9.25f, -1};
        state.scale = 1;
        state.opacity = i ? .4f : 1.f;
        state.visible = state.shadowEnabled = 1;
        Require(session.SetAccessory(std::uint8_t(i), state, reason), reason);
        Require(session.Register({{editor::Track::Accessory, std::uint8_t(i), 0, 0}}, reason),
                reason);
    }
    Require(session.ImportPicture(root / "composite.bmp", 512, 512, reason), reason);
    Require(session.SelectModel(255, reason), reason);
    const auto inputs = [](auto& d, auto&) {
        d.picture.offsetX = 37;
        d.picture.offsetY = -13;
        d.picture.scale = 20;
        d.physics.accessoryRenderOrder = 1;
        d.physics.playbackMode = 0;
        d.physics.projectedShadowAmbient = .375f;
        d.physics.projectedShadowBlend = 1;
        d.view.groundGrid = d.view.groundShadow = 1;
        d.timeline.editMode = 2;
        d.cameraState = {0, 10, 0, 0, 0, -45, 0, 0, 0};
        d.envelope.settings.cameraFov = 30;
        d.cameraPerspective = 0;
        d.lightState = {.6f, .6f, .6f, -.5f, -1, .5f};
        return true;
    };
    Require(session.Edit("original composite inputs", inputs, reason), reason);
    render::SceneRenderSources sources;
    sources.models = session.Assets().modelPaths;
    sources.accessories = session.Assets().accessoryPaths;
    sources.picture = session.Assets().picture;
    render::SceneFrameProducer producer;
    Require(producer.Load(session.Runtime(), sources, assets, reason), reason);
    rhi::BackendOptions options;
    options.backend = rhi::DefaultBackend();
    if (const auto* backend = std::getenv("MDS_GPU_BACKEND")) {
        const std::string name = backend;
        if (name == "d3d11")
            options.backend = rhi::Backend::D3D11;
        else if (name == "d3d12")
            options.backend = rhi::Backend::D3D12;
        else if (name == "vulkan")
            options.backend = rhi::Backend::Vulkan;
        else if (name == "metal")
            options.backend = rhi::Backend::Metal;
        else
            throw std::runtime_error("invalid backend");
    }
    if (const auto* driver = std::getenv("MDS_GPU_DRIVER"))
        if (std::string(driver) == "software")
            options.driver = rhi::BackendDriver::Software;
    if (const auto* library = std::getenv("MDS_TEST_VULKAN_LIBRARY"))
        options.vulkanLibrary = library;
    bool accepted = true;
    for (bool posed : {false, true}) {
        if (posed) {
            Require(
                session.SetBone(0, 0, {.125f, .25f, -.125f}, {0, .1f, 0, std::sqrt(.99f)}, reason),
                reason);
            Require(session.Register({{editor::Track::Bone, 0, 0, 0}}, reason), reason);
            Require(session.SetMorph(0, 2, .625f, reason), reason);
            Require(session.RegisterMorph(0, 2, reason), reason);
            Require(session.Seek(0, reason), reason);
            Require(session.Edit("original pending view", inputs, reason), reason);
        }
        const unsigned size = posed ? 257 : 512;
        render::SceneRenderSettings settings;
        settings.width = settings.height = size;
        settings.shadow.mapSize = 512;
        settings.shadow.mode = 1;
        settings.editorModel = 255;
        settings.postProcessCopy = false;
        rhi::RenderFrame frame;
        Require(producer.Build(session.Runtime(), settings, frame, reason), reason);
        auto device = rhi::CreateOffscreenBackendDevice(size, size, options);
        Require(bool(device), device.reason);
        rhi::SceneRenderer renderer(*device.device);
        rhi::PreparedModelFrame prepared;
        auto result = renderer.Prepare(1, frame, prepared);
        Require(bool(result), result.reason);
        rhi::CompletionToken done;
        result = renderer.Submit(prepared, done);
        Require(bool(result), result.reason);
        rhi::AttachmentReadback actual, repeated;
        Require(renderer.ReadTarget(1, frame.finalColor, actual), "read composite output");
        if (argc == 4) {
            const auto directory = std::filesystem::u8path(argv[3]);
            std::filesystem::create_directories(directory);
            std::ofstream dump(directory / (posed ? "posed.rgba" : "rest.rgba"), std::ios::binary);
            dump.write(reinterpret_cast<const char*>(actual.bytes.data()), actual.bytes.size());
            Require(bool(dump), "write composite diagnostic");
        }
        std::ifstream file(root / (posed ? "posed.png" : "rest.png"), std::ios::binary);
        const std::vector<char> encoded((std::istreambuf_iterator<char>(file)), {});
        rhi::RgbaImage image;
        Require(rhi::DecodePngRgba(encoded.data(), encoded.size(), image),
                "decode native reference");
        rhi::AttachmentReadback expected;
        expected.description = {image.width, image.height, rhi::TextureFormat::Rgba8};
        expected.bytes.assign(image.pixels.begin(), image.pixels.end());
        const auto diff = shadow_acceptance::Compare(actual, expected);
        const auto visual = composite_visual::Compare(actual, expected);
        std::cout << (posed ? "posed" : "rest")
                  << " native composite: outside=" << diff.outsideTolerance
                  << " coverage=" << diff.coverageDifferences << " max=" << diff.maximum << '\n';
        std::cout << "visual composite: mean=" << visual.mean
                  << " largeFraction=" << visual.largePixelFraction
                  << " blockMaximum=" << visual.blockMeanMaximum
                  << " alphaDifferences=" << visual.alphaDifferences
                  << " accepted=" << visual.Accepted() << '\n';
        const bool strict = std::getenv("MDS_COMPOSITE_STRICT") != nullptr;
        accepted &= strict ? diff.Accepted() : visual.Accepted();
        Require(bool(renderer.Submit(prepared, done)), "repeat composite submission");
        Require(renderer.ReadTarget(1, frame.finalColor, repeated), "repeat composite read");
        Require(actual.bytes == repeated.bytes, "repeat composite must be byte exact");
        if (argc == 4) {
            // Keep each actual draw prefix for diagnosis on another GPU. This
            // never changes the final image/reference or its acceptance rule.
            auto partial = frame;
            if (options.backend == rhi::Backend::D3D11)
                for (auto& target : partial.targets)
                    if (target.description.format == rhi::TextureFormat::DepthStencil) {
                        // The negotiated depth contract deliberately forbids
                        // raw transfer. Name DX11's identical physical format
                        // explicitly for this diagnostic copy of the frame.
                        target.description.format = rhi::TextureFormat::Depth24Stencil8;
                        target.description.access |= std::uint32_t(rhi::TextureAccess::CopySource);
                    }
            const auto directory = std::filesystem::u8path(argv[3]);
            const std::string stem = posed ? "posed" : "rest";
            std::ofstream positions(directory / (stem + "-positions.tsv"));
            Require(bool(positions), "write position probe manifest");
            for (auto& pass : partial.passes) {
                if (!pass.color || pass.color->target != frame.finalColor)
                    continue;
                const auto draws = pass.draws;
                for (std::size_t n = 1; n <= draws.size(); ++n) {
                    const auto& draw = draws[n - 1];
                    const auto& pipeline = draw.pipeline;
                    // Export only the actual default model VS path. Screen,
                    // self-shadow/outline and generic programs have distinct
                    // vertex shaders and must not be described by this probe.
                    if (!draw.shader && pipeline.shadow.mode == rhi::BuiltinShadowMode::None &&
                        pipeline.postProcess.mode == rhi::PostProcessMode::None &&
                        (pipeline.vertexLayout == rhi::VertexLayout::PositionColor ||
                         pipeline.vertexLayout == rhi::VertexLayout::PositionNormalUv ||
                         rhi::AdditionalVertexUvs(pipeline.vertexLayout))) {
                        rhi::detail::ModelConstants constants;
                        const auto viewport =
                            pipeline.viewport.value_or(rhi::ModelViewport{0, 0, size, size, 0, 1});
                        Require(
                            rhi::detail::BuildModelConstants(pipeline, viewport, false, constants),
                            "prepare actual vertex constants for probe");
                        for (const auto& resource : frame.resources)
                            if (resource.key.id == draw.vertices.id &&
                                resource.key.version == draw.vertices.version &&
                                resource.kind == rhi::ReplayResourceKind::Vertices) {
                                // Preserve the actual primitive order, including
                                // indexed and nonzero-start draws. SO captures
                                // an assembled list, not unused resource bytes.
                                const auto stride = rhi::ModelVertexStride(pipeline.vertexLayout);
                                std::vector<char> ordered;
                                const auto appendVertex = [&](std::uint32_t index) {
                                    Require(index < resource.bytes.size() / stride,
                                            "position probe vertex range");
                                    const auto* begin =
                                        reinterpret_cast<const char*>(resource.bytes.data()) +
                                        std::size_t(index) * stride;
                                    ordered.insert(ordered.end(), begin, begin + stride);
                                };
                                if (draw.indexed) {
                                    const rhi::ModelReplayResource* indices = nullptr;
                                    for (const auto& candidate : frame.resources)
                                        if (candidate.key.id == draw.indices.id &&
                                            candidate.key.version == draw.indices.version &&
                                            candidate.kind == rhi::ReplayResourceKind::Indices)
                                            indices = &candidate;
                                    Require(indices, "position probe index resource");
                                    const auto width =
                                        indices->indexFormat == rhi::IndexFormat::UInt16 ? 2u : 4u;
                                    Require(draw.firstIndex <= indices->bytes.size() / width &&
                                                draw.indexCount <=
                                                    indices->bytes.size() / width - draw.firstIndex,
                                            "position probe index range");
                                    for (std::uint32_t i = 0; i < draw.indexCount; ++i) {
                                        std::uint32_t index = 0;
                                        std::memcpy(&index,
                                                    indices->bytes.data() +
                                                        std::size_t(draw.firstIndex + i) * width,
                                                    width);
                                        Require(index < draw.vertexCount,
                                                "position probe index bound");
                                        appendVertex(index);
                                    }
                                } else {
                                    Require(draw.firstVertex <= resource.bytes.size() / stride &&
                                                draw.drawVertexCount <=
                                                    resource.bytes.size() / stride -
                                                        draw.firstVertex,
                                            "position probe nonindexed range");
                                    for (std::uint32_t i = 0; i < draw.drawVertexCount; ++i)
                                        appendVertex(draw.firstVertex + i);
                                }
                                const auto prefix = stem + "-draw-" + std::to_string(n);
                                std::ofstream vertices(directory / (prefix + ".vertices"),
                                                       std::ios::binary);
                                vertices.write(ordered.data(), ordered.size());
                                std::ofstream buffer(directory / (prefix + ".constants"),
                                                     std::ios::binary);
                                buffer.write(reinterpret_cast<const char*>(&constants.vertex),
                                             sizeof(constants.vertex));
                                Require(bool(vertices) && bool(buffer),
                                        "write position probe inputs");
                                positions << prefix << '\t' << unsigned(pipeline.vertexLayout)
                                          << '\t' << unsigned(pipeline.topology) << '\n';
                                break;
                            }
                    }
                    pass.draws.assign(draws.begin(), draws.begin() + n);
                    rhi::PreparedModelFrame prefix;
                    result = renderer.Prepare(1, partial, prefix);
                    Require(bool(result), result.reason);
                    result = renderer.Submit(prefix, done);
                    Require(bool(result), result.reason);
                    rhi::AttachmentReadback pixels;
                    Require(renderer.ReadTarget(1, frame.finalColor, pixels), "read draw prefix");
                    if (n == draws.size())
                        Require(pixels.bytes == actual.bytes,
                                "diagnostic final prefix must retain the original frame bytes");
                    const auto name =
                        std::string(posed ? "posed" : "rest") + "-prefix-" + std::to_string(n);
                    std::ofstream dump(directory / (name + ".rgba"), std::ios::binary);
                    dump.write(reinterpret_cast<const char*>(pixels.bytes.data()),
                               pixels.bytes.size());
                    Require(bool(dump), "write draw prefix");
                    if (options.backend == rhi::Backend::D3D11 && pass.depthStencil) {
                        // DX11's negotiated attachment is D24_UNORM_S8_UINT.
                        // Preserve its raw bits beside color to distinguish
                        // coverage/shading from depth/stencil rejection. This
                        // diagnostic never changes the frame or comparison.
                        rhi::AttachmentReadback depth;
                        Require(renderer.ReadTarget(1, pass.depthStencil->target, depth),
                                "read DX11 draw-prefix depth/stencil");
                        Require(depth.bytes.size() == std::size_t(size) * size * 4,
                                "DX11 packed D24S8 byte count");
                        std::ofstream raw(directory / (name + ".d24s8"), std::ios::binary);
                        raw.write(reinterpret_cast<const char*>(depth.bytes.data()),
                                  depth.bytes.size());
                        Require(bool(raw), "write DX11 draw-prefix depth/stencil");
                    }
                }
            }
            positions.flush();
            Require(bool(positions), "complete position probe manifest");
        }
    }
    Require(accepted, "original composite acceptance failed");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
