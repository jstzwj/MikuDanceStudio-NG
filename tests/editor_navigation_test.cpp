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
    {
        editor::EditorSession views;
        CHECK(views.ImportModel(std::filesystem::absolute(argv[1]) / "pose.pmd", reason));
        auto c = views.Runtime().Session()->Pose().camera;
        c.position = {5, 7, 9};
        c.rotation = {};
        c.distance = -23;
        CHECK(views.SetCamera(c, reason));
        CHECK(views.Register({{editor::Track::Camera, 255, 0, 0}}, reason));
        CHECK(views.Seek(12, reason));
        CHECK(views.SetBone(0, 3, {4, 5, 6}, {0, 0, 0, 1}, reason));
        c.distance = -91;
        c.rotation = {.1f, .2f, .3f};
        CHECK(views.SetCamera(c, reason));
        const auto frame = views.Document().timeline.currentFrame;
        const auto* snapshot = views.Runtime().Snapshot(0);
        const auto updates = views.Runtime().Counters().frames;
        CHECK(views.ViewPreset(0, reason));
        CHECK(views.Document().timeline.currentFrame == frame);
        CHECK(views.Document().models[0].bonePose[3].components[0] == 4);
        CHECK(views.Runtime().Snapshot(0) == snapshot &&
              views.Runtime().Counters().frames == updates);
        CHECK(views.Document().cameraState[0] == 5 && views.Document().cameraState[1] == 7);
        CHECK(views.Document().cameraState[3] == 5 && views.Document().cameraState[4] == 7);
        CHECK(views.Document().cameraState[5] == -14 && views.Document().cameraState[6] == 0);
        CHECK(views.ViewPreset(1, reason));
        CHECK(views.Document().cameraState[5] == -30 && views.Document().cameraState[4] == 0);
        CHECK(views.Document().cameraState[6] == 1.5707999467849731f);
        CHECK(views.ViewPreset(2, reason));
        CHECK(views.Document().cameraState[5] == -45 && views.Document().cameraState[4] == 10);
        CHECK(views.ViewPreset(4, reason));
        CHECK(views.Document().cameraState[7] == 1.5707999467849731f);
        CHECK(views.ViewPreset(6, reason));
        CHECK(views.Document().cameraState[7] == -1.5707999467849731f);
        CHECK(views.ViewPreset(5, reason));
        CHECK(views.Document().cameraState[6] == -1.5707999467849731f &&
              views.Document().cameraState[4] == 0);
        CHECK(views.ViewPreset(8, reason));
        CHECK(views.Document().cameraState[7] == 3.1415920257568359f);
        const auto unsupportedRevision = views.Revision();
        for (unsigned key : {3, 7, 9, 42})
            CHECK(views.ViewPreset(key, reason));
        CHECK(views.Revision() == unsupportedRevision);
        CHECK(views.Edit(
            "bone-reference",
            [](auto& d, auto&) {
                d.timeline.cameraReferenceMode = 2;
                return true;
            },
            reason));
        CHECK(views.ViewPreset(2, reason));
        CHECK(views.Document().cameraState[4] == 0);
        CHECK(views.SelectModel(255, reason));
        CHECK(views.ViewPreset(0, reason));
        CHECK(views.Document().cameraState[6] == 1.5707999467849731f);
        CHECK(views.Document().cameraState[5] == -45);
        CHECK(views.Document().cameraState[0] == 5 && views.Document().cameraState[1] == 7);
        c = views.Runtime().Session()->Pose().camera;
        c.parentModel = 0;
        c.parentBone = 3;
        c.position = {5, 7, -21};
        CHECK(views.SetCamera(c, reason));
        CHECK(views.Edit(
            "saved-basis",
            [](auto& d, auto&) {
                d.appearance.cameraAttachmentBasis = cpu_math::RotationY(.7f);
                return true;
            },
            reason));
        const auto basis = views.Document().appearance.cameraAttachmentBasis;
        const auto preparedFrames = views.Runtime().Counters().frames;
        const auto* preparedSnapshot = views.Runtime().Snapshot(0);
        CHECK(views.PrepareView(reason));
        CHECK(views.Document().cameraState[3] == 5 && views.Document().cameraState[4] == 7 &&
              views.Document().cameraState[5] == -21);
        CHECK(views.Document().appearance.cameraAttachmentBasis == basis);
        const auto preparedRevision = views.Revision();
        CHECK(views.PrepareView(reason));
        CHECK(views.Revision() == preparedRevision);
        c = views.Runtime().Session()->Pose().camera;
        c.parentModel = -1;
        CHECK(views.SetCamera(c, reason));
        CHECK(views.PrepareView(reason));
        CHECK(views.Document().appearance.cameraAttachmentBasis == cpu_math::Identity());
        CHECK(views.SelectModel(0, reason));
        CHECK(views.PrepareView(reason));
        CHECK(views.Document().appearance.cameraAttachmentBasis == cpu_math::Identity());
        CHECK(views.Runtime().Counters().frames == preparedFrames &&
              views.Runtime().Snapshot(0) == preparedSnapshot);
        CHECK(views.Play(reason));
        const auto playingState = views.Document().cameraState;
        const auto playingRevision = views.Revision();
        CHECK(views.ViewPreset(4, reason));
        CHECK(views.Document().cameraState == playingState && views.Revision() == playingRevision);
        views.Pause();
        CHECK(!views.Runtime().Playing());
    }
    {
        editor::EditorSession reference;
        CHECK(reference.ImportModel(std::filesystem::absolute(argv[1]) / "pose.pmd", reason));
        CHECK(reference.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        CHECK(reference.SelectBones({}, -1, reason));
        auto c = reference.Runtime().Session()->Pose().camera;
        c.rotation = {};
        c.parentModel = -1;
        c.distance = -45;
        CHECK(reference.SetCamera(c, reason));
        const auto state = reference.Document().cameraState;
        const auto* snapshot = reference.Runtime().Snapshot(0);
        const auto frames = reference.Runtime().Counters().frames;
        CHECK(reference.SetCameraReference(2, reason));
        // The command falls back to bone zero, unlike frame setup's no-active
        // pivot. Retain this original distinction rather than normalizing it.
        CHECK(reference.Document().cameraState[3] == state[3] - 1);
        CHECK(reference.Document().cameraState[4] == state[4] - 2);
        CHECK(reference.Document().cameraState[5] == state[5] - 3);
        const auto revision = reference.Revision();
        const auto changed = reference.Document().cameraState;
        CHECK(reference.SetCameraReference(2, reason));
        CHECK(reference.Revision() == revision);
        CHECK(!reference.SetCameraReference(3, reason));
        CHECK(reference.Document().cameraState == changed && reference.Revision() == revision);
        CHECK(reference.SelectModel(255, reason));
        CHECK(reference.SetCameraReference(1, reason));
        CHECK(reference.Document().cameraState == changed); // camera mode only changes metadata
        CHECK(reference.Runtime().Counters().frames == frames &&
              reference.Runtime().Snapshot(0) == snapshot);
        CHECK(reference.SelectModel(0, reason));
        CHECK(reference.Undo(reason)); // view changes do not consume bone history
        CHECK(reference.Document().models[0].bonePose[0].components[0] == 0);
        CHECK(reference.Document().timeline.cameraReferenceMode == 1 &&
              reference.Document().cameraState == changed);
    }
    std::cout
        << "View navigation: native modifiers, parent/follow gates and zero scene updates passed\n";
}
