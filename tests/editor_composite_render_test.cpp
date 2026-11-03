#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "self_shadow_compare.hpp"
#include <cmath>
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
        std::cout << (posed ? "posed" : "rest")
                  << " native composite: outside=" << diff.outsideTolerance
                  << " coverage=" << diff.coverageDifferences << " max=" << diff.maximum << '\n';
        accepted &= diff.Accepted();
        Require(bool(renderer.Submit(prepared, done)), "repeat composite submission");
        Require(renderer.ReadTarget(1, frame.finalColor, repeated), "repeat composite read");
        Require(actual.bytes == repeated.bytes, "repeat composite must be byte exact");
    }
    Require(accepted, "original composite pixels differ");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
