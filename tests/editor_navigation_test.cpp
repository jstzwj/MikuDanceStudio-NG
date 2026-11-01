#include "mikudancestudio/editor_session.hpp"
#include <cmath>
#include <iostream>
#include <limits>
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
    CHECK(argc == 2);
    editor::EditorSession session;
    CHECK(session.ImportModel(std::filesystem::absolute(argv[1]) / "pose.pmd", reason));
    CHECK(session.SetBone(0, 3, {1, 2, 3}, {0, 0, 0, 1}, reason));
    const auto* runtime = &session.Runtime();
    const auto* snapshot = runtime->Snapshot(0);
    const auto* vertices = snapshot->vertices.data();
    const auto counters = runtime->Counters();
    const auto set = [&](float depth = -45) {
        auto camera = session.Runtime().Session()->Pose().camera;
        camera.position = {0, 0, depth};
        camera.rotation = {};
        camera.distance = -45;
        camera.fov = 30;
        camera.parentModel = -1;
        camera.parentBone = 0;
        return session.SetCamera(camera, reason);
    };
    CHECK(set());
    CHECK(session.DragView(false, 20, -10, false, false, reason));
    CHECK(std::abs(session.Document().cameraState[6] - .1f) < 1e-7f);
    CHECK(std::abs(session.Document().cameraState[7] + .2f) < 1e-7f);
    const auto angles = session.Runtime().Session()->Pose().camera.rotation;
    const float initialX = session.Document().cameraState[3],
                initialY = session.Document().cameraState[4];
    CHECK(session.DragView(false, 20, -10, true, true, reason));
    CHECK(session.Runtime().Session()->Pose().camera.rotation == angles);
    CHECK(session.Document().cameraState[3] == initialX - 2);
    CHECK(session.Document().cameraState[4] == initialY - 1);
    CHECK(session.Document().cameraState[5] == -45); // model Shift wins over Ctrl
    CHECK(session.DragView(false, 20, -10, false, true, reason));
    CHECK(session.Document().cameraState[5] == -44);
    CHECK(session.DragView(true, 20, -10, false, false, reason));
    CHECK(session.Document().cameraState[3] == initialX - 3);
    CHECK(session.Document().cameraState[4] == initialY - 1.5f);
    CHECK(session.DragView(true, 20, -10, true, true, reason));
    CHECK(session.Document().cameraState[3] == initialX - 13);
    CHECK(session.Document().cameraState[4] == initialY - 6.5f);
    CHECK(session.DragView(true, 20, -10, false, true, reason));
    CHECK(std::abs(session.Document().cameraState[3] - (initialX - 13.1f)) < 1e-6f);
    CHECK(session.WheelView(120, reason));
    CHECK(session.Document().cameraState[5] == -38);
    CHECK(session.WheelView(65536 + 120, reason)); // Win32 signed 16-bit wheel payload
    CHECK(session.Document().cameraState[5] == -32);
    const auto revision = session.Revision();
    CHECK(session.WheelView(0, reason));
    CHECK(session.Revision() == revision);
    CHECK(session.CanUndo());
    CHECK(&session.Runtime() == runtime && runtime->Snapshot(0) == snapshot);
    CHECK(snapshot->vertices.data() == vertices);
    CHECK(runtime->Counters().frames == counters.frames &&
          runtime->Counters().preparedModels == counters.preparedModels);
    CHECK(session.Undo(reason));
    CHECK(session.Document().models[0].bonePose[3].components[0] == 0);
    CHECK(session.Document().cameraState[5] == -32);
    CHECK(!session.CanUndo());

    CHECK(session.SelectModel(255, reason));
    CHECK(set());
    const auto before = session.Runtime().Session()->Pose().camera;
    CHECK(session.DragView(false, 20, -10, true, false, reason));
    CHECK(std::abs(session.Document().cameraState[7] + .2f) < 1e-7f); // camera Shift still orbits
    CHECK(session.Runtime().Session()->Pose().camera.position == before.position);
    CHECK(set());
    CHECK(session.DragView(true, 20, -10, false, false, reason));
    CHECK(session.Document().cameraState[0] == -1 && session.Document().cameraState[1] == -.5f);
    CHECK(session.Document().cameraState[2] == -45);
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.rotation[1] = 1.5707963267948966f;
    CHECK(session.SetCamera(camera, reason));
    CHECK(session.DragView(true, 20, 0, false, false, reason));
    CHECK(std::abs(session.Document().cameraState[2] + 46) <
          1e-6f); // yaw rotates screen X into world Z
    for (float depth : {0.f, .3f, .300001f, .5f, 1.f, 30.f, -30.f}) {
        CHECK(set(depth));
        camera = session.Runtime().Session()->Pose().camera;
        camera.parentModel = 0;
        CHECK(session.SetCamera(camera, reason));
        CHECK(session.DragView(true, 20, -10, false, false, reason));
        const float factor = std::abs(depth) <= .3f ? 10.f : std::max(std::abs(depth), 1.f);
        CHECK(std::abs(session.Document().cameraState[0] + .012f * factor) < 1e-6f);
        CHECK(std::abs(session.Document().cameraState[1] + .006f * factor) < 1e-6f);
        CHECK(session.WheelView(-120, reason));
        CHECK(session.Document().cameraState[2] == depth - 6);
        CHECK(session.Document().cameraState[5] == -45);
    }
    CHECK(session.SelectModel(0, reason));
    CHECK(session.Edit(
        "follow",
        [](auto& doc, auto&) {
            doc.appearance.followCamera = 1;
            return true;
        },
        reason));
    const auto followed = session.Document().cameraState;
    const auto followRevision = session.Revision(),
               followFrames = session.Runtime().Counters().frames;
    CHECK(session.DragView(false, 20, 10, false, false, reason));
    CHECK(session.WheelView(120, reason));
    CHECK(session.Document().cameraState == followed && session.Revision() == followRevision);
    CHECK(session.DragView(true, 20, 10, false, false, reason));
    CHECK(session.Document().cameraState[3] == followed[3] - 1);
    CHECK(session.Runtime().Counters().frames == followFrames);
    const auto valid = session.Document().cameraState;
    const auto validRevision = session.Revision();
    camera = session.Runtime().Session()->Pose().camera;
    camera.position[0] = std::numeric_limits<float>::infinity();
    CHECK(!session.SetCamera(camera, reason));
    CHECK(session.Document().cameraState == valid && session.Revision() == validRevision);
    camera = session.Runtime().Session()->Pose().camera;
    camera.parentModel = 254;
    CHECK(!session.SetCamera(camera, reason));
    CHECK(session.Revision() == validRevision);
    CHECK(session.Play(reason));
    const auto epoch = session.PlaybackEpoch(),
               playbackFrames = session.Runtime().Counters().frames;
    CHECK(session.DragView(false, 20, 10, false, false, reason));
    CHECK(session.Playing() && session.PlaybackEpoch() == epoch);
    CHECK(session.Runtime().Counters().frames == playbackFrames);
    CHECK(session.Advance(1. / 30., reason));
    CHECK(session.Runtime().Counters().frames == playbackFrames + 1);
    CHECK(session.Playing());
    std::cout
        << "View navigation: native modifiers, parent/follow gates and zero scene updates passed\n";
}
