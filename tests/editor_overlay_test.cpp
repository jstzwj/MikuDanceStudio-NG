#include "mikudancestudio/bone_overlay.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_editor_overlay.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <cstdlib>
#include <iostream>
#include <tuple>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    CHECK(argc == 3);
    {
        render::ViewportTools tools;
        tools.camera = false;
        tools.editMode = 3;
        tools.centerX = 400;
        tools.centerY = 300;
        const auto hit = [&](int x, int y) {
            return render::ViewportToolAt(tools, x, y, 1280, 960, 1);
        };
        CHECK(hit(420, 300) == 3 && hit(400, 320) == 4 && hit(440, 300) == 5);
        CHECK(hit(407, 300) == 0 && hit(435, 300) == 0 && hit(445, 300) == 0);
        CHECK(hit(1170, 895) == 9 && hit(1210, 895) == 10 && hit(1250, 935) == 14);
        CHECK(hit(1153, 895) == 0 && hit(1170, 880) == 0);
        CHECK(hit(1170, 865) == 21);
        tools.editMode = 4;
        CHECK(hit(450, 300) == 6 && hit(400, 250) == 7 && hit(400, 300) == 8);
        CHECK(hit(441, 300) == 0 && hit(400, 239) == 0);
        tools.camera = true;
        CHECK(hit(1170, 895) == 18 && hit(1250, 935) == 17);
        tools.playing = true;
        CHECK(hit(1170, 895) == 0 && hit(1170, 865) == 0);
        std::vector<std::array<float, 8>> quads;
        const auto draw = [&] {
            quads.clear();
            render::AppendViewportTools(
                tools, 1280, 960, 1,
                [&](float l, float t, float r, float b, float u0, float v0, float u1, float v1) {
                    quads.push_back({l, t, r, b, u0, v0, u1, v1});
                });
        };
        draw();
        CHECK(quads.empty());
        tools.playing = false;
        tools.camera = false;
        tools.operation = 7;
        tools.coordinateSystem = 1;
        draw();
        CHECK(quads.size() == 4);
        CHECK(quads.front() == (std::array<float, 8>{1150, 845, 1270, 875, .001953f, .52929902f,
                                                     .23242199f, .58594f}));
        CHECK(quads.back() == (std::array<float, 8>{387, 233, 462, 308, .75f, 0.f, 1.f, .25f}));
        tools.operation = 13;
        draw();
        CHECK(quads.size() == 5);
        CHECK(quads[3][0] == 1190 && quads[3][1] == 920);
    }
    {
        std::vector<std::array<float, 4>> lines;
        render::AppendSelectionBox(70, 90, 10, 20,
                                   [&](float x, float y, float ex, float ey, std::uint32_t color) {
                                       lines.push_back({x, y, ex, ey});
                                       if (color != 0xFFFFFFFFu)
                                           throw std::runtime_error("selection color differs");
                                   });
        CHECK(lines ==
              (std::vector<std::array<float, 4>>{
                  {10, 20, 10, 90}, {10, 90, 70, 90}, {70, 90, 70, 20}, {70, 20, 10, 20}}));
    }
    // Frozen native atlas cells and order. These checks do not obtain expected
    // UVs from the producer, and include the selected mode-1 physics exception.
    {
        struct Bone {
            cpu_bone::BoneType type = cpu_bone::BoneType::Move;
            std::uint16_t flags = cpu_bone::kBoneFlagVisible;
            bool hasRigidBody = false, physicsDisabled = false;
            int screenX = 100, screenY = 200, tailScreenX = 120, tailScreenY = 220, tailBone = 1;
        } bone;
        std::vector<std::tuple<int, float, float>> markers;
        const auto draw = [&](int mode, int physics, bool selected, bool edited) {
            markers.clear();
            cpu_bone::OverlayMarkers(
                1, [&](int) -> const auto& { return bone; }, [&](int) { return selected; },
                [&](int) { return edited; }, 0, mode, physics,
                [&](int index, float v0, float v1) { markers.emplace_back(index, v0, v1); });
        };
        draw(0, 0, false, false);
        CHECK(markers == (std::vector<std::tuple<int, float, float>>{{0, .56179601f, .59639901f},
                                                                     {0, .38601601f, .42061901f}}));
        draw(0, 0, false, true);
        CHECK(std::get<1>(markers[0]) == .210236f);
        draw(0, 0, true, true);
        CHECK(std::get<1>(markers[0]) == .034456f);
        draw(2, 0, true, true);
        CHECK(markers.empty());
        draw(3, 0, true, false);
        CHECK(markers.size() == 1 && std::get<1>(markers[0]) == .38601601f);
        bone.hasRigidBody = true;
        draw(0, 1, true, false);
        CHECK(markers.size() == 1);
        draw(0, 2, true, false);
        CHECK(markers.empty());
        bone.physicsDisabled = true;
        draw(0, 2, true, false);
        CHECK(markers.size() == 2);
        bone.hasRigidBody = false;
        bone.type = cpu_bone::BoneType::UnderIk;
        bone.flags |= cpu_bone::kBoneFlagFixedAxis;
        draw(0, 0, false, true);
        CHECK(std::get<1>(markers[0]) == .73812902f && std::get<2>(markers[1]) == .80788797f);
        struct Relation {
            int boneIndex = 0, targetBone = 1;
            bool enabled = true;
            std::uint8_t linkCount = 1;
            const std::uint16_t* links;
        };
        const std::uint16_t member = 0;
        Relation relations[2]{{0, 1, true, 1, &member}, {0, 1, true, 1, &member}};
        std::vector<std::uint32_t> colors;
        const auto lineCount = cpu_bone::OverlayLines(
            2, [&](int) -> const auto& { return bone; }, 2,
            [&](int i) -> const auto& { return relations[i]; }, [](int) { return false; },
            [](int) { return false; }, -1, 0, 0, true, 1.f,
            [&](int, int, int, int, float, std::uint32_t color) {
                colors.push_back(color);
                return true;
            },
            [&](int, int, int, int, std::uint32_t color) { colors.push_back(color); });
        CHECK(lineCount == 8);
        CHECK(colors == (std::vector<std::uint32_t>{0xffff9b00u, 0xffff9b00u, 0xff6464e6u,
                                                    0xff000000u, 0xff000000u}));
    }
    const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    rhi::FileAssetSource assets(std::filesystem::absolute(std::filesystem::u8path(argv[2])));
    rhi::BackendOptions options;
    options.backend = rhi::Backend::Vulkan;
    if (const auto* value = std::getenv("MDS_GPU_BACKEND")) {
        if (std::string(value) == "d3d11")
            options.backend = rhi::Backend::D3D11;
        if (std::string(value) == "d3d12")
            options.backend = rhi::Backend::D3D12;
    }
    if (const auto* value = std::getenv("MDS_GPU_DRIVER"))
        if (std::string(value) == "software")
            options.driver = rhi::BackendDriver::Software;
    if (const auto* value = std::getenv("MDS_TEST_VULKAN_LIBRARY"))
        options.vulkanLibrary = value;
    auto gpu = rhi::CreateOffscreenBackendDevice(640, 480, options);
    CHECK(gpu);
    rhi::SceneRenderer renderer(*gpu.device);
    unsigned cases = 0;
    for (const char* name : {"pose.pmd", "triangle.pmx", "articulated.pmd", "overlay-offset.pmx",
                             "overlay-types.pmd"}) {
        editor::EditorSession session;
        CHECK(session.ImportModel(root / name, reason));
        CHECK(session.Edit(
            "overlay-view",
            [](auto& doc, auto&) {
                doc.view.groundGrid = doc.view.groundShadow = doc.selfShadowMode = 0;
                doc.physics.playbackMode = 0;
                return true;
            },
            reason));
        CHECK(session.SelectBones({0}, 0, reason));
        render::SceneRenderSources sources;
        sources.models = session.Assets().modelPaths;
        render::SceneFrameProducer producer;
        render::SceneEditorOverlay overlay;
        CHECK(producer.Load(session.Runtime(), sources, assets, reason));
        CHECK(overlay.Load(assets, reason));
        render::SceneRenderSettings settings;
        settings.width = 640;
        settings.height = 480;
        settings.editorModel = 0;
        settings.shadow.mode = 0;
        const auto frames = session.Runtime().Counters().frames;
        const auto submit = [&](const rhi::RenderFrame& frame, rhi::AttachmentReadback& pixels) {
            rhi::PreparedModelFrame prepared;
            rhi::CompletionToken token;
            auto result = renderer.Prepare(1, frame, prepared);
            if (result)
                result = renderer.Submit(prepared, token);
            reason = result.reason;
            return result && renderer.ReadTarget(1, frame.finalColor, pixels);
        };
        rhi::RenderFrame scene;
        CHECK(producer.Build(session.Runtime(), settings, scene, reason));
        rhi::AttachmentReadback plain, actual, repeat;
        CHECK(submit(scene, plain));
        rhi::ReplayResourceKey lease;
        for (int mode : {0, 1, 2, 3, 4, 0}) {
            session.SetEditMode(mode);
            CHECK(producer.Build(session.Runtime(), settings, scene, reason));
            auto frame = scene;
            CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false,
                                 frame, reason));
            CHECK(submit(frame, actual) && submit(frame, repeat));
            CHECK(actual.bytes == repeat.bytes);
            if (mode == 2)
                CHECK(actual.bytes == plain.bytes);
            else {
                CHECK(frame.passes.back().name == "editor_bone_overlay");
                CHECK(frame.passes.back().color->target == frame.finalColor);
                CHECK(actual.bytes != plain.bytes);
                lease = frame.passes.back().draws.back().vertices;
            }
            // Playback hides markers, retaining immutable upload leases.
            auto hidden = scene;
            CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, true,
                                 hidden, reason));
            CHECK(submit(hidden, repeat));
            CHECK(repeat.bytes == plain.bytes);
            CHECK(overlay.Load(assets, reason));
            CHECK(submit(frame, repeat) && repeat.bytes == actual.bytes);
            ++cases;
        }
        // White box feedback loads finalColor and preserves all upload lifetimes.
        render::ViewportTools tools;
        tools.selectionBox = true;
        tools.box = {300, 220, 120, 100};
        session.SetEditMode(1);
        CHECK(producer.Build(session.Runtime(), settings, scene, reason));
        auto boxed = scene;
        CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false, boxed,
                             reason, &tools));
        CHECK(submit(boxed, actual) && submit(boxed, repeat) && actual.bytes == repeat.bytes);
        tools.selectionBox = false;
        auto unboxed = scene;
        CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false, unboxed,
                             reason, &tools));
        CHECK(submit(unboxed, repeat) && actual.bytes != repeat.bytes);
        CHECK(session.Runtime().Counters().frames == frames);
        CHECK(session.SelectModel(255, reason));
        settings.editorModel = 255;
        CHECK(producer.Build(session.Runtime(), settings, scene, reason));
        CHECK(overlay.Append(session.Runtime(), settings, {}, false, scene, reason));
        CHECK(submit(scene, actual));
        CHECK(scene.passes.back().draws.back().vertices.id == lease.id);
        settings.width = 511;
        settings.height = 383;
        CHECK(producer.Build(session.Runtime(), settings, scene, reason));
        CHECK(overlay.Append(session.Runtime(), settings, {}, false, scene, reason));
        CHECK(submit(scene, actual));
        CHECK(actual.description.width == 511 && actual.description.height == 383);
        CHECK(session.Runtime().Counters().frames == frames);
        CHECK(renderer.Unload(1));
    }
    {
        editor::EditorSession empty;
        CHECK(empty.Edit(
            "clear-only",
            [](auto& doc, auto&) {
                doc.view.groundGrid = doc.view.groundShadow = doc.selfShadowMode = 0;
                return true;
            },
            reason));
        render::SceneFrameProducer producer;
        render::SceneEditorOverlay overlay;
        CHECK(producer.Load(empty.Runtime(), {}, assets, reason));
        CHECK(overlay.Load(assets, reason));
        render::SceneRenderSettings settings;
        settings.width = 640;
        settings.height = 480;
        settings.editorModel = 255;
        settings.postProcessCopy = false;
        settings.shadow.mode = 0;
        rhi::RenderFrame scene, frame;
        CHECK(producer.Build(empty.Runtime(), settings, scene, reason));
        CHECK(scene.resources.empty());
        std::uint64_t firstVersion = 0;
        for (bool playing : {false, true, true, false}) {
            frame = scene;
            CHECK(overlay.Append(empty.Runtime(), settings, {}, playing, frame, reason));
            if (playing)
                CHECK(frame.resources.empty());
            else {
                CHECK(!frame.resources.empty());
                const auto version = frame.passes.back().draws.back().vertices.version;
                if (firstVersion)
                    CHECK(version > firstVersion);
                firstVersion = version;
            }
            rhi::PreparedModelFrame prepared;
            rhi::CompletionToken token;
            auto result = renderer.Prepare(2, frame, prepared);
            reason = result.reason;
            CHECK(result);
            CHECK(renderer.Submit(prepared, token));
        }
        CHECK(renderer.Unload(2));
    }
    std::cout << "Editor overlay: native atlas/filter/IK rules and " << cases
              << " GPU mode cases passed\n";
}
