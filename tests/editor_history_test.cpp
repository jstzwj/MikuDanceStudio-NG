#include "mikudancestudio/editor_session.hpp"
#include <iostream>
using namespace mikudancestudio;
using namespace editor;
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
    const auto root = std::filesystem::absolute(argv[1]);
    EditorSession s;
    CHECK(s.ImportModel(root / "pose.pmd", reason));
    CHECK(!s.CanUndo() && !s.CanRedo());
    CHECK(s.SetBone(0, 1, {1, 2, 3}, {0, 0, 0, 1}, reason));
    CHECK(s.Document().models[0].bonePose[1].flags[1] == 1);
    CHECK(s.ImportModel(root / "pose.pmd", reason));
    CHECK(!s.CanUndo());
    CHECK(s.SetBone(1, 1, {7, 8, 9}, {0, 0, 0, 1}, reason));
    auto camera = s.Runtime().Session()->Pose().camera;
    camera.distance = -17;
    CHECK(s.SetCamera(camera, reason));
    s.Selected().model = 255;
    CHECK(!s.CanUndo() && !s.Undo(reason));
    s.Selected().model = 0;
    s.Selected().bones = {5};
    CHECK(s.Undo(reason));
    CHECK(s.Document().models[0].bonePose[1].components[0] == 0);
    CHECK(s.Document().models[1].bonePose[1].components[0] == 7);
    CHECK(s.Runtime().Session()->Pose().camera.distance == -17);
    CHECK(s.Selected().model == 0 && s.Selected().bones == (std::vector<std::uint32_t>{5}));
    CHECK(!s.SetBone(0, 1, {}, {}, reason) && s.CanRedo());
    CHECK(s.Redo(reason));
    CHECK(s.Document().models[0].bonePose[1].components[0] == 1);
    CHECK(s.Document().models[0].bonePose[1].flags[1] == 1);
    CHECK(s.SetMorph(0, 0, .5f, reason));
    CHECK(s.Undo(reason));
    CHECK(s.Document().models[0].morphPose[0] == .5f);
    CHECK(s.Document().models[0].bonePose[1].components[0] == 0);
    CHECK(s.Redo(reason));
    // A frame change captures all edited models independently and clears markers.
    CHECK(s.Seek(12, reason));
    CHECK(s.Document().models[0].bonePose[1].flags[1] == 0);
    CHECK(s.Undo(reason));
    CHECK(s.Document().timeline.currentFrame == 0);
    CHECK(s.Document().models[0].bonePose[1].components[0] == 1);
    CHECK(s.Document().models[1].bonePose[1].components[0] == 0);
    CHECK(s.Redo(reason));
    CHECK(s.Document().timeline.currentFrame ==
          0); // native pose redo does not restore the later cursor
    CHECK(s.Document().models[0].bonePose[1].components[0] == 0);
    CHECK(s.Undo(reason)); // preserves original pose-with-frame entry across cycles
    CHECK(s.Document().models[0].bonePose[1].components[0] == 1);
    s.Selected().model = 1;
    CHECK(s.Undo(reason));
    CHECK(s.Document().models[1].bonePose[1].components[0] == 7);
    CHECK(s.Document().models[0].bonePose[1].components[0] == 1);
    // Unload is not undoable and invalidates only this model's history.
    CHECK(s.UnloadModel(1, reason));
    CHECK(!s.CanUndo());
    s.Selected().model = 0;
    CHECK(s.CanUndo());
    CHECK(s.ImportModel(root / "pose.pmd", reason));
    CHECK(!s.CanUndo());
    // Ring capacity and redo branch truncation.
    EditorSession ring;
    CHECK(ring.ImportModel(root / "pose.pmd", reason));
    for (int i = 1; i <= 35; ++i)
        CHECK(ring.SetBone(0, 1, {float(i), 0, 0}, {0, 0, 0, 1}, reason));
    for (int i = 0; i < 30; ++i)
        CHECK(ring.Undo(reason));
    CHECK(!ring.CanUndo() && ring.Document().models[0].bonePose[1].components[0] == 5);
    for (int i = 0; i < 30; ++i)
        CHECK(ring.Redo(reason));
    CHECK(!ring.CanRedo() && ring.Document().models[0].bonePose[1].components[0] == 35);
    CHECK(ring.Undo(reason));
    CHECK(ring.SetBone(0, 2, {4, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(!ring.CanRedo());
    // One continuous gesture, including unchanged transforms, is one entry.
    CHECK(ring.BeginGesture());
    CHECK(ring.SetBone(0, 1, {6, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(ring.SetBone(0, 1, {8, 0, 0}, {0, 0, 0, 1}, reason));
    ring.EndGesture();
    CHECK(ring.Undo(reason));
    CHECK(ring.Document().models[0].bonePose[1].components[0] == 34);
    CHECK(ring.Document().models[0].bonePose[2].components[0] == 4);
    // Mixed registration: native history restores only bone keys and poses.
    CHECK(ring.Register({{Track::Bone, 0, 1, 20}, {Track::Morph, 0, 0, 20}}, reason));
    CHECK(ring.Document().models[0].bonePose[1].flags[1] == 0);
    CHECK(ring.Undo(reason));
    CHECK(ring.Document().models[0].boneKeys.sparse.empty());
    CHECK(ring.Document().models[0].morphKeys.sparse.size() == 1);
    CHECK(ring.Redo(reason));
    CHECK(ring.Document().models[0].boneKeys.sparse.size() == 1);
    CHECK(ring.Play(reason));
    CHECK(!ring.CanUndo() && !ring.CanRedo() && !ring.Undo(reason));
    ring.Pause();
    // Registration clears this model's key marks before history capture. Undo
    // restores only touched slots and preserves subsequent unrelated selection.
    EditorSession registration;
    CHECK(registration.ImportModel(root / "pose.pmd", reason));
    CHECK(registration.ImportModel(root / "pose.pmd", reason));
    registration.Selected().model = 0;
    registration.Selected().keys = {{Track::Bone, 0, 1, 0},
                                    {Track::Morph, 0, 0, 0},
                                    {Track::Display, 0, 0, 0},
                                    {Track::Bone, 1, 1, 0}};
    CHECK(registration.Register({{Track::Bone, 0, 1, 10}}, reason));
    CHECK(registration.Selected().keys.size() == 2);
    CHECK(registration.Selected().keys.count({Track::Bone, 0, 1, 10}));
    CHECK(registration.Selected().keys.count({Track::Bone, 1, 1, 0}));
    for (const auto& key : registration.Document().models[0].boneKeys.dense)
        CHECK(!key.allocated);
    CHECK(!registration.Document().models[0].morphKeys.dense[0].allocated);
    CHECK(!registration.Document().models[0].displayKeys.dense[0].allocated);
    registration.Selected().keys.insert({Track::Bone, 0, 2, 0});
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        CHECK(registration.Undo(reason));
        CHECK(registration.Document().models[0].boneKeys.sparse.empty());
        CHECK(registration.Selected().keys.size() == 2);
        CHECK(registration.Selected().keys.count({Track::Bone, 0, 2, 0}));
        CHECK(registration.Selected().keys.count({Track::Bone, 1, 1, 0}));
        CHECK(registration.Redo(reason));
        CHECK(registration.Selected().keys.size() == 3);
        CHECK(registration.Selected().keys.count({Track::Bone, 0, 1, 10}));
    }
    CHECK(registration.Delete({{Track::Bone, 0, 1, 10}}, reason));
    CHECK(registration.Document().models[0].selection.maxFrame == 10);
    CHECK(registration.Register({{Track::Bone, 0, 1, 5}}, reason));
    CHECK(registration.Document().models[0].selection.maxFrame == 10);
    CHECK(registration.Document().timeline.lastRegisteredFrame == 10);
    std::cout
        << "Per-model native history: scope, 30 slots, frame snapshots, gestures and invalidation passed\n";
    return 0;
}
