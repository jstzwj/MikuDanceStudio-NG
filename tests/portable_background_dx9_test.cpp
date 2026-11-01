#include "background_dx9_reference.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "self_shadow_compare.hpp"
#include <iostream>
using namespace mikudancestudio;
void Require(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    rhi::BackendOptions options;
    options.backend = std::string(argv[2]) == "d3d12"    ? rhi::Backend::D3D12
                      : std::string(argv[2]) == "vulkan" ? rhi::Backend::Vulkan
                                                         : rhi::Backend::D3D11;
    rhi::FileAssetSource assets(std::filesystem::u8path(argv[1]));
    BackgroundDx9Reference oracle;
    unsigned failures = 0, cases = 0;
    for (unsigned fixture : {4u, 14u, 8u}) {
        auto image =
            std::make_shared<const rhi::RgbaImage>(BackgroundImage(kBackgroundImages[fixture]));
        for (unsigned width : {192u, 511u, 1024u})
            for (bool offset : {false, true}) {
                const unsigned height = width * 3 / 4;
                editor::EditorSession session;
                std::string reason;
                Require(session.Edit(
                            "background-oracle",
                            [&](auto& d, auto&) {
                                d.view.groundGrid = d.view.groundShadow = d.selfShadowMode = 0;
                                d.picture.enabled = 1;
                                d.picture.offsetX = offset ? 37 : 0;
                                d.picture.offsetY = offset ? -13 : 0;
                                d.picture.scale = 1280.f / image->width * (offset ? .37f : 1.f);
                                return true;
                            },
                            reason),
                        reason);
                render::SceneRenderSources sources;
                sources.picture = image;
                render::SceneFrameProducer producer;
                Require(producer.Load(session.Runtime(), sources, assets, reason), reason);
                render::SceneRenderSettings settings;
                settings.width = width;
                settings.height = height;
                settings.shadow.mode = 0;
                auto device = rhi::CreateOffscreenBackendDevice(width, height, options);
                Require(bool(device), device.reason);
                rhi::SceneRenderer renderer(*device.device);
                for (unsigned frame = 0; frame < 2; ++frame) {
                    rhi::RenderFrame draws;
                    Require(producer.Build(session.Runtime(), settings, draws, reason), reason);
                    rhi::PreparedModelFrame prepared;
                    rhi::CompletionToken token;
                    auto result = renderer.Prepare(1, draws, prepared);
                    Require(bool(result), result.reason);
                    result = renderer.Submit(prepared, token);
                    Require(bool(result), result.reason);
                    rhi::AttachmentReadback actual;
                    Require(bool(renderer.ReadTarget(1, draws.finalColor, actual)),
                            "background readback");
                    const auto expected = oracle.Render(*image, session.Document().picture, width,
                                                        height, frame != 0);
                    const auto difference = shadow_acceptance::Compare(actual, expected);
                    unsigned alphaMax = 0;
                    for (std::size_t i = 3; i < actual.bytes.size(); i += 4)
                        alphaMax = std::max(alphaMax, unsigned(std::abs(int(actual.bytes[i]) -
                                                                        int(expected.bytes[i]))));
                    std::cout << "background fixture=" << fixture << " width=" << width
                              << " offset=" << offset << " frame=" << frame
                              << " max=" << difference.maximum << " alpha=" << alphaMax
                              << " outside=" << difference.outsideTolerance
                              << " coverage=" << difference.coverageDifferences << '\n';
                    failures += !difference.Accepted();
                    if (!difference.Accepted()) {
                        unsigned printed = 0;
                        for (std::size_t i = 0; i < actual.bytes.size() && printed < 4; i += 4)
                            if (actual.bytes[i] != expected.bytes[i]) {
                                std::cout << "  pixel " << (i / 4) % width << ',' << (i / 4) / width
                                          << " actual=" << unsigned(actual.bytes[i]) << ','
                                          << unsigned(actual.bytes[i + 1]) << ','
                                          << unsigned(actual.bytes[i + 2]) << ','
                                          << unsigned(actual.bytes[i + 3])
                                          << " native=" << unsigned(expected.bytes[i]) << ','
                                          << unsigned(expected.bytes[i + 1]) << ','
                                          << unsigned(expected.bytes[i + 2]) << ','
                                          << unsigned(expected.bytes[i + 3]) << '\n';
                                ++printed;
                            }
                    }
                    ++cases;
                    producer.Commit();
                }
            }
    }
    std::cout << "background native comparison: " << cases << " cases, failures=" << failures
              << '\n';
    return failures ? 1 : 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
