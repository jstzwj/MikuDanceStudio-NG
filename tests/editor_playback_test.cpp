#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_edit.hpp"
#include <cmath>
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
    editor::EditorSession session;
    const auto root = std::filesystem::absolute(argv[1]);
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    CHECK(session.Edit(
        "animated-globals",
        [](auto& d, auto&) {
            auto camera = d.cameraKeys.dense[0];
            camera.eye = {};
            camera.target = {};
            camera.parentModel = -1;
            camera.distance = -45;
            for (unsigned c = 0; c < 6; ++c) {
                camera.interpolation[0][c] = camera.interpolation[1][c] = 20;
                camera.interpolation[2][c] = camera.interpolation[3][c] = 107;
            }
            auto cameraEnd = camera;
            cameraEnd.frame = 30;
            cameraEnd.eye[0] = 30;
            auto light = d.lightKeys.dense[0];
            light.color = {0, .5f, 1};
            auto lightEnd = light;
            lightEnd.frame = 30;
            lightEnd.color[0] = 1;
            auto shadow = d.selfShadowKeys.dense[0];
            shadow.mode = 0;
            shadow.distance = .01f;
            auto shadowEnd = shadow;
            shadowEnd.frame = 30;
            shadowEnd.mode = 2;
            auto gravity = d.gravityKeys.dense[0];
            gravity.acceleration = 2;
            auto gravityEnd = gravity;
            gravityEnd.frame = 30;
            gravityEnd.acceleration = 10;
            auto accessory = d.accessories[0].keyRecords.dense[0];
            accessory.position = {};
            accessory.scale = 1;
            auto accessoryEnd = accessory;
            accessoryEnd.frame = 30;
            accessoryEnd.position[0] = 60;
            accessoryEnd.scale = 3;
            auto bone = d.models[0].boneKeys.dense[0];
            bone.frame = 30;
            bone.position[0] = 30;
            d.cameraState[0] = 91;
            d.cameraState[3] = 13;
            d.cameraState[4] = 14;
            d.lightState[0] = .9f;
            d.selfShadowMode = 1;
            d.physics.gravityMagnitude = 17;
            d.accessories[0].state.position[0] = 88;
            d.timeline.playStopFrame = 300;
            d.timeline.startAtCurrent = 1;
            return format::BuildPmmTrack<format::PmmCameraKey>({{camera, cameraEnd}},
                                                               d.cameraKeys) &&
                   format::BuildPmmTrack<format::PmmLightKey>({{light, lightEnd}}, d.lightKeys) &&
                   format::BuildPmmTrack<format::PmmSelfShadowKey>({{shadow, shadowEnd}},
                                                                   d.selfShadowKeys) &&
                   format::BuildPmmTrack<format::PmmGravityKey>({{gravity, gravityEnd}},
                                                                d.gravityKeys) &&
                   format::BuildPmmTrack<format::PmmAccessoryKey>({{accessory, accessoryEnd}},
                                                                  d.accessories[0].keyRecords) &&
                   format::RegisterPmmKey(d.models[0].boneKeys, 0, bone);
        },
        reason));
    const auto& d = session.Document();
    CHECK(session.Seek(15, reason));
    CHECK(d.cameraState[0] == 91 && d.cameraState[3] == 13 && d.cameraState[4] == 14);
    CHECK(d.lightState[0] == .9f && d.selfShadowMode == 1 && d.physics.gravityMagnitude == 17);
    CHECK(d.accessories[0].state.position[0] == 88 &&
          Near(d.models[0].bonePose[0].components[0], 15));
    CHECK(session.Play(reason));
    CHECK(session.Document().cameraState[0] == 91 && session.Document().lightState[0] == .9f);
    CHECK(session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 2));
    CHECK(!session.Runtime().Session()->Playback().camera.active);
    const auto frames = session.Runtime().Counters().frames;
    CHECK(session.Advance(.1, reason));
    CHECK(session.Document().cameraState[0] == 91 && session.Document().lightState[0] == .9f);
    CHECK(session.Document().selfShadowMode == 1 &&
          session.Document().physics.gravityMagnitude == 17);
    CHECK(session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 2.2f));
    CHECK(session.Runtime().Counters().frames == frames + 1);
    session.Pause();
    CHECK(session.SelectModel(255, reason));
    CHECK(session.Seek(15, reason));
    CHECK(Near(session.Document().cameraState[0], 15));
    CHECK(session.Document().cameraState[3] == 0 && session.Document().cameraState[4] == 0);
    CHECK(session.Document().lightState[0] == .5f && session.Document().selfShadowMode == 0);
    CHECK(session.Document().physics.gravityMagnitude == 6 &&
          session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 2));
    CHECK(session.Runtime().Simulation().gravityMagnitude == 6);
    CHECK(session.SelectModel(0, reason));
    CHECK(session.SetFollowCamera(true, reason));
    CHECK(session.Seek(12, reason));
    CHECK(Near(session.Document().cameraState[0], 12) &&
          Near(session.Document().cameraState[3], 12));
    CHECK(session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 1.8f));
    CHECK(!session.Document().appearance.suppressCameraTransform);
    CHECK(session.SetFollowCamera(false, reason));
    const auto held = session.Document().cameraState;
    CHECK(session.Seek(20, reason));
    CHECK(session.Document().cameraState == held &&
          Near(session.Document().accessories[0].state.scale, 1.8f));
    CHECK(session.SelectModel(255, reason));
    CHECK(session.Seek(0, reason));
    CHECK(session.Play(reason));
    CHECK(session.Advance(1, reason));
    CHECK(
        session.Runtime().Session()->Playback().camera.active); // exact terminal key remains active
    CHECK(session.Advance(.1, reason));
    const auto& playback = session.Runtime().Session()->Playback();
    CHECK(!playback.camera.active && !playback.light.active && !playback.selfShadow.active &&
          !playback.gravity.active);
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.position = {123, 456, 789};
    camera.distance = -999;
    CHECK(session.SetCamera(camera, reason));
    const auto manual = session.Document().cameraState;
    CHECK(session.Advance(.1, reason));
    CHECK(session.Document().cameraState == manual);
    session.Pause();
    // Standalone/export hosts keep the default full-track seek behavior.
    format::SceneSession standalone;
    CHECK(format::SceneSession::Create(session.Document(), standalone, reason));
    CHECK(standalone.Seek(15));
    CHECK(Near(standalone.Pose().camera.position[0], 15) && standalone.Pose().lightState[0] == .5f);
    CHECK(standalone.Pose().accessories[0].position[0] == 0 &&
          Near(standalone.Pose().accessories[0].scale, 2));
    CHECK(standalone.StartPlayback());
    const auto initial = standalone.Pose();
    CHECK(standalone.Advance(.5f, {false, false})); // physics substep: models only
    CHECK(standalone.Pose().camera.position == initial.camera.position &&
          standalone.Pose().lightState == initial.lightState);
    CHECK(standalone.Pose().accessories[0].position == initial.accessories[0].position);
    CHECK(standalone.Playback().camera.index == 0 &&
          standalone.Playback().accessories[0].index == 0);
    CHECK(standalone.Advance(.5f));
    CHECK(Near(standalone.Pose().camera.position[0], 15) &&
          Near(standalone.Pose().accessories[0].scale, 2));
    std::cout
        << "Editor track application: model/follow/camera gates, accessories, terminal cursors and substeps passed\n";
}
