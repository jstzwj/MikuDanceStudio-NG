#include "mikudancestudio/bone_overlay.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_editor_overlay.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <cstdlib>
#include <iostream>
#include <tuple>
#include <algorithm>
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
    CHECK(render::ViewportToolRestorePoint(9, 1280, 960, 1) == (std::array<int, 2>{1170, 895}));
    CHECK(render::ViewportToolRestorePoint(14, 1280, 960, 1) == (std::array<int, 2>{1250, 935}));
    CHECK(render::ViewportToolRestorePoint(18, 511, 383, .4790625f) ==
          (std::array<int, 2>{459, 352}));
    CHECK(render::ViewportHeaderToolAt(952, 88, 1000, 100) == 1);
    CHECK(render::ViewportHeaderToolAt(982, 88, 1000, 100) == 2);
    CHECK(render::ViewportHeaderToolAt(940, 88, 1000, 100) == 0);
    CHECK(render::ViewportHeaderToolAt(964, 88, 1000, 100) == 0);
    CHECK(render::ViewportHeaderToolAt(970, 88, 1000, 100) == 0);
    CHECK(render::ViewportHeaderToolAt(994, 88, 1000, 100) == 0);
    CHECK(render::ViewportHeaderToolAt(952, 75, 1000, 100) == 0);
    CHECK(render::ViewportHeaderToolAt(982, 100, 1000, 100) == 0);
    CHECK(render::ViewportHeaderRestorePoint(1, 1000, 100) == (std::array<int, 2>{952, 88}));
    CHECK(render::ViewportHeaderRestorePoint(2, 1000, 100) == (std::array<int, 2>{982, 88}));
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
    {
        std::vector<std::uint8_t> bytes;
        CHECK(assets.Read(rhi::AssetId::AxisMesh, bytes));
        std::shared_ptr<const runtime::AccessoryResource> axis;
        // Ordinary accessories now support missing normals and vertex colors.
        // The operation-mesh entry still has its own flat-normal/material rules.
        CHECK(runtime::AccessoryResource::Parse(bytes.data(), bytes.size(), axis, reason));
        CHECK(axis->Materials()[0].ambient != (std::array<float, 4>{}));
        CHECK(runtime::AccessoryResource::ParseOperationMesh(bytes.data(), bytes.size(), axis,
                                                             reason));
        CHECK(axis->Materials().size() == 6 && axis->Subsets().size() == 6);
        CHECK(!axis->Vertices().empty() && !axis->Indices().empty());
        for (const auto& m : axis->Materials())
            CHECK(m.diffuse[3] == 1 && m.ambient == (std::array<float, 4>{}) && m.texture.empty());
        for (const auto& v : axis->Vertices()) {
            const float length =
                v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2];
            CHECK(std::abs(length - 1) < 2e-6f);
        }
        const auto saved = axis;
        CHECK(!runtime::AccessoryResource::ParseOperationMesh(bytes.data(), bytes.size() - 3, axis,
                                                              reason) &&
              axis == saved);
    }
    rhi::BackendOptions options;
    options.backend = rhi::DefaultBackend();
    if (const auto* value = std::getenv("MDS_GPU_BACKEND")) {
        if (std::string(value) == "d3d11")
            options.backend = rhi::Backend::D3D11;
        if (std::string(value) == "d3d12")
            options.backend = rhi::Backend::D3D12;
        if (std::string(value) == "vulkan")
            options.backend = rhi::Backend::Vulkan;
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
        // The operation mesh has a fresh depth attachment, before all 2D feedback.
        // Removing only its pass proves that the visible change comes from 3D geometry.
        for (int coordinate : {0, 1}) {
            tools.coordinateSystem = coordinate;
            tools.operation = 9;
            auto axis = scene;
            CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false, axis,
                                 reason, &tools));
            const auto found =
                std::find_if(axis.passes.begin(), axis.passes.end(),
                             [](const auto& p) { return p.name == "editor_operation_axis"; });
            CHECK(found != axis.passes.end() && found->draws.size() == 6);
            CHECK(found->color->target == axis.finalColor &&
                  found->color->load == rhi::AttachmentLoad::Load);
            CHECK(found->depthStencil && found->depthStencil->load == rhi::AttachmentLoad::Clear);
            CHECK(submit(axis, actual) && submit(axis, repeat) && actual.bytes == repeat.bytes);
            const auto vertices = found->draws[0].vertices;
            axis.passes.erase(found);
            CHECK(submit(axis, repeat) && actual.bytes != repeat.bytes);
            auto hidden = scene;
            CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, true,
                                 hidden, reason, &tools));
            CHECK(std::none_of(hidden.passes.begin(), hidden.passes.end(),
                               [](const auto& p) { return p.name == "editor_operation_axis"; }));
            CHECK(std::any_of(hidden.resources.begin(), hidden.resources.end(), [&](const auto& r) {
                return r.key.id == vertices.id && r.key.version == vertices.version;
            }));
        }
        tools.operation = 0;
        tools.dragMode = 4;
        auto dragged = scene;
        CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false, dragged,
                             reason, &tools));
        CHECK(std::any_of(dragged.passes.begin(), dragged.passes.end(),
                          [](const auto& p) { return p.name == "editor_operation_axis"; }));
        CHECK(submit(dragged, actual));
        if (session.Runtime().Resource(0)->PmdSource()) {
            // Independent synthetic staging data: every shape, an unselected
            // joint marker, the selected joint axis, and live body snapshots.
            editor::ModelPhysicsEdit edit;
            for (unsigned shape = 0; shape < 3; ++shape) {
                const int body = edit.AddBody();
                edit.bodies[body].shape = std::uint8_t(shape);
                edit.bodies[body].size[0] = .8f;
                edit.bodies[body].size[1] = 2;
                edit.bodies[body].size[2] = 1;
                edit.bodies[body].position[0] = (float(shape) - 1) * 3;
                edit.bodies[body].position[1] = 10;
            }
            const int joint = edit.AddJoint();
            edit.joints[joint].position[1] = 10;
            edit.joints[joint].rigidA = 1;
            edit.joints[joint].rigidB = 2;
            tools.physicsEdit = {&edit, 0, 1, joint, false};
            std::vector<render::PhysicsGizmoDraw> draws;
            std::uint32_t next;
            CHECK(render::PhysicsGizmoDraws(session.Runtime(), tools.physicsEdit, 0xffff0000, draws,
                                            next, reason));
            CHECK(draws.size() == 6 && draws[0].argb == 0xffff0000 &&
                  draws[1].mesh == render::PhysicsGizmoMesh::Joint && draws[2].argb == 0xffff0000 &&
                  draws[3].argb == 0xff9bff00);
            for (bool jointPage : {false, true}) {
                tools.physicsEdit.jointPage = jointPage;
                auto preview = scene;
                CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false,
                                     preview, reason, &tools));
                CHECK(std::none_of(preview.passes.begin(), preview.passes.end(), [](const auto& p) {
                    return p.name == "editor_operation_axis";
                }));
                CHECK(submit(preview, actual) && submit(preview, repeat) &&
                      actual.bytes == repeat.bytes);
                preview.passes.erase(std::remove_if(preview.passes.begin(), preview.passes.end(),
                                                    [](const auto& p) {
                                                        return p.name == "editor_physics_overlay";
                                                    }),
                                     preview.passes.end());
                CHECK(submit(preview, repeat) && actual.bytes != repeat.bytes);
                CHECK(overlay.Commit() == !jointPage);
                auto repeated = scene;
                CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false,
                                     repeated, reason, &tools));
                CHECK(submit(repeated, repeat));
                CHECK(!overlay.Commit());
                ++cases;
            }
            tools.physicsEdit = {};
            if (!session.Runtime().PhysicsDebugBodies().empty()) {
                settings.display.physicsBodies = true;
                auto live = scene;
                CHECK(overlay.Append(session.Runtime(), settings, {}, true, live, reason, &tools));
                CHECK(std::any_of(live.passes.begin(), live.passes.end(), [](const auto& p) {
                    return p.name == "editor_physics_overlay";
                }));
                CHECK(submit(live, actual));
                settings.display.physicsBodies = false;
            }
        }
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
        if (std::string(name) == "pose.pmd") {
            CHECK(session.SelectModel(0, reason));
            settings.editorModel = 0;
            tools.operation = 12;
            tools.dragMode = 0;
            CHECK(producer.Build(session.Runtime(), settings, scene, reason));
            CHECK(overlay.Append(session.Runtime(), settings, session.Selected().bones, false,
                                 scene, reason, &tools));
            CHECK(submit(scene, actual));
            auto replacement = rhi::CreateOffscreenBackendDevice(511, 383, options);
            CHECK(replacement && renderer.Rebind(*replacement.device));
            std::swap(gpu, replacement);
            CHECK(submit(scene, repeat) && repeat.bytes == actual.bytes);
        }
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
        std::uint64_t firstVersion = 0, axisVersion = 0;
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
                const auto axis =
                    std::find_if(frame.resources.begin(), frame.resources.end(),
                                 [](const auto& r) { return r.key.id == 0x7000000000000003ull; });
                CHECK(axis != frame.resources.end());
                CHECK(axis->key.version > axisVersion);
                axisVersion = axis->key.version;
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
