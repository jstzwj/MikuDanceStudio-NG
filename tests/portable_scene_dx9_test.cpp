#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "self_shadow_dx9_reference.hpp"
#include "self_shadow_compare.hpp"
#include <iostream>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
namespace {
void Require(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc < 3)
        return 2;
    const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto assets = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    BackendOptions options;
    options.backend = argc > 3 && std::string(argv[3]) == "d3d12"    ? Backend::D3D12
                      : argc > 3 && std::string(argv[3]) == "vulkan" ? Backend::Vulkan
                                                                     : Backend::D3D11;
    shadow_reference::Reference oracle(assets / "skin_effect_sm3.fx");
    FileAssetSource files(assets);
    unsigned cases = 0;
    for (const char* name :
         {"bound-varied.x", "sphere-mul.x", "sphere-add.x", "dual-sphere.x", "tint-shadow.pmx",
          "uv1-only.pmx", "uv1-dual.pmx", "uv4-only.pmx", "uv4-dual.pmx"})
        for (int mode : {1, 2}) {
            editor::EditorSession session;
            std::string reason;
            const bool model = std::filesystem::path(name).extension() == ".pmx";
            Require(model ? session.ImportModel(root / name, reason)
                          : session.ImportAccessory(root / name, reason),
                    reason);
            Require(session.Edit(
                        "oracle-view",
                        [&](auto& d, auto&) {
                            d.view.groundGrid = d.view.groundShadow = 0;
                            d.selfShadowMode = mode;
                            if (!model) {
                                auto& a = d.accessories[0].state;
                                a.position = {1, 2, 3};
                                a.rotation = {.1f, .2f, .3f};
                                a.scale = .2f;
                                a.opacity = .7f;
                            }
                            return true;
                        },
                        reason),
                    reason);
            if (model) {
                Require(session.SetMorph(0, 1, .5f, reason), reason);
                Require(session.SetMorph(0, 2, .5f, reason), reason);
                if (std::string(name).rfind("uv", 0) == 0)
                    Require(session.SetMorph(0, 3, .75f, reason), reason);
            }
            render::SceneRenderSources sources;
            sources.models = session.Assets().modelPaths;
            sources.accessories = session.Assets().accessoryPaths;
            render::SceneFrameProducer producer;
            Require(producer.Load(session.Runtime(), sources, files, reason), reason);
            render::SceneRenderSettings settings;
            settings.width = 192;
            settings.height = 144;
            settings.shadow.mode = mode;
            settings.shadow.mapSize = 128;
            settings.postProcessCopy = false;
            RenderFrame frame;
            Require(producer.Build(session.Runtime(), settings, frame, reason), reason);
            auto backend = CreateOffscreenBackendDevice(settings.width, settings.height, options);
            Require(bool(backend), backend.reason);
            SceneRenderer renderer(*backend.device);
            PreparedModelFrame prepared;
            CompletionToken completion;
            std::vector<std::uint8_t> lit;
            for (bool forceShadow : {false, true}) {
                if (forceShadow) {
                    frame.passes[0].draws.clear();
                    frame.passes[0].color->clear = {0, 0, 0, 1};
                }
                auto result = renderer.Prepare(1, frame, prepared);
                Require(bool(result), result.reason);
                result = renderer.Submit(prepared, completion);
                Require(bool(result), result.reason);
                oracle.Render(frame);
                for (const auto target : {std::uint64_t(3), frame.finalColor}) {
                    AttachmentReadback actual;
                    Require(bool(renderer.ReadTarget(1, target, actual)), "read_target");
                    const auto expected = oracle.Read(target);
                    auto diff = shadow_acceptance::Compare(actual, expected);
                    std::size_t coverage = 0;
                    for (std::size_t i = 0; i < actual.bytes.size(); i += 4) {
                        if (target == 3) {
                            float depth;
                            std::memcpy(&depth, actual.bytes.data() + i, 4);
                            coverage += depth < 1;
                        } else
                            coverage += actual.bytes[i] != 255 || actual.bytes[i + 1] != 255 ||
                                        actual.bytes[i + 2] != 255;
                    }
                    std::cout << name << " mode=" << mode << " forced=" << forceShadow
                              << " target=" << target << " max=" << diff.maximum
                              << " outside=" << diff.outsideTolerance
                              << " coverageDiff=" << diff.coverageDifferences
                              << " drawn=" << coverage << '\n';
                    Require(coverage > 0, "empty_scene_or_shadow_map");
                    Require(diff.Accepted(), "DX9 scene comparison failed");
                    if (target == frame.finalColor) {
                        if (!forceShadow)
                            lit = actual.bytes;
                        else if (!model)
                            Require(lit != actual.bytes, "accessory_shadow_branch_not_observed");
                    }
                    Require(bool(renderer.Submit(prepared, completion)), "repeat_submit");
                    AttachmentReadback repeated;
                    Require(bool(renderer.ReadTarget(1, target, repeated)), "repeat_read");
                    Require(actual.bytes == repeated.bytes, "repeat_not_exact");
                }
                ++cases;
            }
        }
    std::cout << "Portable production scene / independent FX9 cases=" << cases << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    const std::string reason = e.what();
    return reason.find("CreateDevice: DX9") != std::string::npos ||
                   reason == "DX9 effect runtime unavailable"
               ? 77
               : 1;
}
