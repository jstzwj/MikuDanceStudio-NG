#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_edit.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Near(float a, float b) {
    return std::abs(a - b) <= 2e-5f;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    editor::EditorSession session;
    for (unsigned i = 0; i < 3; ++i)
        CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    CHECK(session.Edit(
        "mode-tracks",
        [](auto& d, auto&) {
            auto camera = d.cameraKeys.dense[0];
            camera.eye = {5, 0, 0};
            camera.target = {};
            camera.parentModel = -1;
            camera.distance = -45;
            for (unsigned c = 0; c < 6; ++c) {
                camera.interpolation[0][c] = camera.interpolation[1][c] = 20;
                camera.interpolation[2][c] = camera.interpolation[3][c] = 107;
            }
            auto cameraEnd = camera;
            cameraEnd.frame = 30;
            cameraEnd.eye[0] = 35;
            auto light = d.lightKeys.dense[0];
            light.color = {.2f, .2f, .2f};
            auto lightEnd = light;
            lightEnd.frame = 30;
            lightEnd.color = {.8f, .8f, .8f};
            auto gravity = d.gravityKeys.dense[0];
            gravity.acceleration = 2;
            auto gravityEnd = gravity;
            gravityEnd.frame = 30;
            gravityEnd.acceleration = 10;
            auto accessory = d.accessories[0].keyRecords.dense[0];
            accessory.scale = 1;
            auto accessoryEnd = accessory;
            accessoryEnd.frame = 30;
            accessoryEnd.scale = 3;
            d.timeline.cameraReferenceMode = 0;
            return format::BuildPmmTrack<format::PmmCameraKey>({{camera, cameraEnd}},
                                                               d.cameraKeys) &&
                   format::BuildPmmTrack<format::PmmLightKey>({{light, lightEnd}}, d.lightKeys) &&
                   format::BuildPmmTrack<format::PmmGravityKey>({{gravity, gravityEnd}},
                                                                d.gravityKeys) &&
                   format::BuildPmmTrack<format::PmmAccessoryKey>({{accessory, accessoryEnd}},
                                                                  d.accessories[0].keyRecords);
        },
        reason));
    CHECK(session.Seek(15, reason));
    CHECK(session.SetBone(2, 3, {1, 2, 3}, {0, 0, 0, 1}, reason));
    CHECK(session.SelectBones({3}, 3, reason));
    session.SetEditMode(4);
    const auto bones = session.Document().models[2].bonePose;
    const auto* runtime = &session.Runtime();
    const auto* snapshot = runtime->Snapshot(2);
    const auto frames = runtime->Counters().frames;
    const auto history = session.CanUndo();
    CHECK(session.SelectModel(255, reason));
    CHECK(session.Selected().model == 255 && session.Document().timeline.editMode == 2);
    CHECK(session.Document().envelope.settings.options[0] == 1);
    CHECK(Near(session.Document().cameraState[0], 20) && session.Document().cameraState[3] == 0);
    CHECK(Near(session.Document().lightState[0], .5f));
    CHECK(session.Document().physics.gravityMagnitude == 6 &&
          session.Runtime().Simulation().gravityMagnitude == 6);
    CHECK(Near(session.Document().accessories[0].state.scale, 2));
    CHECK(session.Document().appearance.cameraAttachmentBasis == cpu_math::Identity());
    CHECK(session.Document().models[2].bonePose[3].components == bones[3].components);
    CHECK(session.SelectModel(2, reason));
    CHECK(session.Document().timeline.editMode == 0 && session.ActiveBone() == 3);
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{3}));
    CHECK(session.Document().envelope.settings.options[0] == 0 &&
          session.Document().envelope.selectedModelSlot == 2);
    CHECK(Near(session.Document().cameraState[3], 20) && session.Document().cameraState[4] == 0);
    CHECK(session.Document().lightState ==
          (std::array<float, 6>{.602f, .602f, .602f, -.5f, -1, .5f}));
    CHECK(&session.Runtime() == runtime && session.Runtime().Snapshot(2) == snapshot &&
          session.Runtime().Counters().frames == frames);
    CHECK(session.CanUndo() == history);
    session.SetEditMode(4);
    CHECK(session.SelectModel(2, reason)); // reactivating a model also resets the edit mode
    CHECK(session.Document().timeline.editMode == 0 && session.ActiveBone() == 3);
    // Following reloads globals on camera/model transitions, but a model/model
    // switch related to the camera parent reloads only the camera track.
    CHECK(session.SetFollowCamera(true, reason));
    CHECK(session.SelectModel(255, reason));
    CHECK(session.SelectModel(2, reason));
    CHECK(Near(session.Document().lightState[0], .5f));
    CHECK(session.Edit(
        "tracked-switch",
        [](auto& d, auto&) {
            for (auto& key : d.cameraKeys.dense) {
                key.parentModel = 0;
                key.parentBone = 0;
            }
            for (auto& key : d.cameraKeys.sparse) {
                key.value.parentModel = 0;
                key.value.parentBone = 0;
            }
            d.appearance.cameraParentModel = 0;
            d.appearance.cameraParentBone = 0;
            d.cameraState[0] = 99;
            d.cameraState[3] = 13;
            d.lightState[0] = .875f;
            d.accessories[0].state.scale = 9;
            return true;
        },
        reason));
    const auto updates = session.Runtime().Counters().frames;
    CHECK(session.SelectModel(1, reason)); // neither old nor new model is tracked
    CHECK(session.Document().cameraState[0] == 99 && session.Document().cameraState[3] == 13);
    CHECK(session.SelectModel(0, reason));
    CHECK(Near(session.Document().cameraState[0], 20));
    CHECK(session.Document().lightState[0] == .875f &&
          session.Document().accessories[0].state.scale == 9);
    CHECK(!session.Document().appearance.suppressCameraTransform);
    CHECK(session.SelectModel(1, reason)); // leaving the tracked model reloads too
    CHECK(Near(session.Document().cameraState[0], 20) && session.Document().lightState[0] == .875f);
    CHECK(session.Runtime().Counters().frames == updates);
    const auto before = session.Document().cameraState;
    const auto revision = session.Revision();
    CHECK(!session.SelectModel(42, reason));
    CHECK(session.Selected().model == 1 && session.Document().cameraState == before &&
          session.Revision() == revision);
    auto candidate = *session.Runtime().Session();
    format::PmmViewRefresh invalid;
    invalid.camera = candidate.Pose().camera;
    invalid.reloadGlobals = true;
    invalid.editorModel = 42;
    CHECK(!candidate.RefreshEditorView(invalid));
    CHECK(candidate.Document().cameraState == before &&
          candidate.Document().lightState[0] == .875f);
    const auto write = [](const auto& path, const auto& bytes, auto&) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return bool(file);
    };
    CHECK(session.Save(root / "model-mode.pmm", write, reason));
    CHECK(session.Open(root / "model-mode.pmm", reason));
    CHECK(session.Selected().model == 1 && session.Document().cameraState == before);
    CHECK(session.SelectModel(255, reason));
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.position[0] = 123;
    CHECK(session.SetCamera(camera, reason));
    CHECK(session.Save(root / "camera-mode.pmm", write, reason));
    CHECK(session.Open(root / "camera-mode.pmm", reason));
    CHECK(session.Selected().model == 255 && session.Document().cameraState[0] == 123);
    CHECK(session.Document().envelope.selectedModelSlot == 1);
    CHECK(session.Play(reason));
    const auto playingRevision = session.Revision();
    CHECK(!session.SelectModel(0, reason));
    CHECK(session.Playing() && session.Selected().model == 255 &&
          session.Revision() == playingRevision);
    session.Pause();
    const auto transientRevision = session.Revision();
    const auto transientFrames = session.Runtime().Counters().frames;
    for (unsigned expected : {1u, 2u, 0u, 1u, 2u}) {
        session.CycleCoordinateSystem();
        CHECK(session.CoordinateSystem() == expected);
    }
    CHECK(session.Revision() == transientRevision &&
          session.Runtime().Counters().frames == transientFrames);
    CHECK(session.SelectModel(1, reason));
    CHECK(session.CoordinateSystem() == 0);
    session.CycleCoordinateSystem();
    CHECK(session.CoordinateSystem() == 1);
    session.CycleCoordinateSystem();
    CHECK(session.CoordinateSystem() == 0);
    std::cout
        << "Editor mode transitions: global/camera reload gates, default light, pose/history preservation and saved mode passed\n";
}
