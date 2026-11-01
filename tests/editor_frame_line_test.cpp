#include "mikudancestudio/editor_session.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>
using namespace mikudancestudio;
using namespace editor;
using namespace format;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << " " << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

template <class T> bool Kernel() {
    PmmKeyTrack<T> source;
    source.dense.resize(2);
    // Deliberately interleaved, nonconsecutive native record identities.
    source.dense[0].next = 13;
    source.dense[1].next = 7;
    T a{}, b{}, c{};
    a.frame = 1;
    a.previous = 0;
    a.next = 19;
    b.frame = 15;
    b.previous = 1;
    c.frame = 30;
    c.previous = 13;
    source.sparse = {{7, 0, b}, {19, 0, c}, {13, 0, a}};
    for (unsigned cursor : {0, 1, 2, 15, 16, 30, 31}) {
        auto track = source;
        unsigned affected = 99;
        if (!InsertPmmFrameLine(track, cursor, affected))
            return false;
        unsigned expected = 0;
        for (unsigned i = 0; i < source.sparse.size(); ++i) {
            const auto& before = source.sparse[i];
            const auto& after = track.sparse[i];
            const bool shift = before.value.frame >= cursor;
            expected += shift;
            if (after.index != before.index || after.value.previous != before.value.previous ||
                after.value.next != before.value.next ||
                after.value.frame != before.value.frame + shift)
                return false;
        }
        if (affected != expected || track.dense[0].frame || track.dense[1].frame ||
            track.dense[0].next != 13 || track.dense[1].next != 7)
            return false;
    }
    source.sparse[1].value.frame = 0x7fffffff;
    unsigned affected = 77;
    if (InsertPmmFrameLine(source, 0, affected) || affected != 77 ||
        source.sparse[0].value.frame != 15 || source.sparse[2].value.frame != 1)
        return false;
    source.sparse[1].value.next = 19;
    return !InsertPmmFrameLine(source, 0, affected);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    CHECK(Kernel<PmmBoneKey>() && Kernel<PmmMorphKey>() && Kernel<PmmCameraKey>() &&
          Kernel<PmmLightKey>());
    const auto root = std::filesystem::absolute(argv[1]);
    const auto writer = [](const auto& path, const auto& bytes, auto&) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return bool(file);
    };
    for (auto track : {Track::Bone, Track::Morph, Track::Camera, Track::Light}) {
        EditorSession session;
        session.SetAutoInterpolation(false); // This fixture intentionally uses linear key curves.
        CHECK(session.ImportModel(root / "triangle.pmx", reason));
        CHECK(session.ImportModel(root / "pose.pmd", reason));
        CHECK(session.SelectModel(track == Track::Bone || track == Track::Morph ? 0 : 255, reason));
        const auto slot = session.Selected().model;
        for (unsigned frame : {1, 15, 30}) {
            if (track == Track::Bone)
                CHECK(session.SetBone(0, 0, {float(frame), 0, 0}, {0, 0, 0, 1}, reason));
            else if (track == Track::Morph)
                CHECK(session.SetMorph(0, 0, float(frame) / 30, reason));
            else if (track == Track::Camera) {
                auto camera = session.Runtime().Session()->Pose().camera;
                camera.position[0] = float(frame);
                CHECK(session.SetCamera(camera, reason));
            } else {
                CHECK(session.Edit(
                    "light-key",
                    [&](auto& doc, auto&) {
                        doc.lightState[0] = float(frame) / 100;
                        return true;
                    },
                    reason));
            }
            CHECK(session.Register({{track, slot, 0, frame}}, reason));
        }
        CHECK(session.Seek(15, reason));
        CHECK(session.Save(root / "frame-line.pmm", writer, reason));
        CHECK(session.Open(root / "frame-line.pmm", reason));
        CHECK(!session.Dirty() && !session.CanUndo());
        CHECK(session.Edit(
            "unregistered-other-model",
            [](auto& doc, auto&) {
                doc.models[1].bonePose[1].components[0] = 7;
                doc.models[1].bonePose[1].flags[1] = 1;
                doc.lightState[0] = .123f;
                doc.cameraState[3] = 3;
                doc.cameraState[4] = -5;
                return true;
            },
            reason));
        const auto before = session.Document();
        session.Selected().keys = {
            {track, slot, 0, 0}, {track, slot, 0, 15}, {Track::Bone, 1, 0, 0}};
        CHECK(session.InsertFrameLine(track, reason));
        CHECK(session.Selected().keys.count({track, slot, 0, 16}));
        CHECK(session.Selected().keys.count({track, slot, 0, 0}));
        CHECK(session.Selected().keys.count({Track::Bone, 1, 0, 0}));
        CHECK(session.Document().timeline.currentFrame == 15);
        CHECK(session.Document().models[1].bonePose[1].components[0] == 7);
        CHECK(session.Document().models[1].bonePose[1].flags[1] == 1);
        if (track != Track::Light)
            CHECK(session.Document().lightState == before.lightState);
        const float expected = 1.f + 14.f * (14.f / 15.f);
        if (track == Track::Bone)
            CHECK(std::abs(session.Document().models[0].bonePose[0].components[0] - expected) <
                  .005f);
        else if (track == Track::Morph)
            CHECK(std::abs(session.Document().models[0].morphPose[0] - expected / 30) < .000001f);
        else if (track == Track::Camera) {
            CHECK(std::abs(session.Document().cameraState[0] - expected) < .00001f);
            CHECK(session.Document().cameraState[3] == 0 && session.Document().cameraState[4] == 0);
        } else
            CHECK(std::abs(session.Document().lightState[0] - expected / 100) < .000001f);
        if (track != Track::Camera)
            CHECK(session.Document().cameraState == before.cameraState);
        CHECK(session.CanUndo() == (track == Track::Bone));
        if (track == Track::Bone) {
            for (unsigned cycle = 0; cycle < 3; ++cycle) {
                // Native undo restores the captured selection of touched keys;
                // its redo recovers the selection displaced by that undo.
                session.Selected().keys.erase({track, slot, 0, 16});
                session.Selected().keys.insert({track, slot, 0, 31});
                CHECK(session.Undo(reason));
                CHECK(session.Selected().keys.count({track, slot, 0, 15}));
                CHECK(!session.Selected().keys.count({track, slot, 0, 30}));
                CHECK(session.Document().models[0].boneKeys.sparse[1].value.frame == 15);
                CHECK(session.Redo(reason));
                CHECK(!session.Selected().keys.count({track, slot, 0, 16}));
                CHECK(session.Selected().keys.count({track, slot, 0, 31}));
                CHECK(session.Document().models[0].boneKeys.sparse[1].value.frame == 16);
            }
        }
        CHECK(session.Save(root / "frame-line.pmm", writer, reason));
        CHECK(session.Open(root / "frame-line.pmm", reason));
        const auto all = session.Keys();
        CHECK(std::any_of(all.begin(), all.end(), [&](const auto& key) {
            return key.track == track && key.slot == slot && key.frame == 31;
        }));
        CHECK(session.Play(reason));
        const auto revision = session.Revision();
        CHECK(!session.InsertFrameLine(track, reason));
        CHECK(session.Playing() && session.Revision() == revision);
    }
    EditorSession empty;
    CHECK(empty.ImportModel(root / "pose.pmd", reason));
    CHECK(empty.Save(root / "empty-line.pmm", writer, reason));
    const auto* runtime = &empty.Runtime();
    const auto revision = empty.Revision();
    CHECK(empty.InsertFrameLine(Track::Bone, reason));
    CHECK(!empty.Dirty() && !empty.CanUndo() && empty.Revision() == revision);
    CHECK(&empty.Runtime() == runtime);
    CHECK(empty.InsertFrameLine(Track::Bone, reason, true));
    CHECK(empty.Dirty() && !empty.CanUndo() && &empty.Runtime() == runtime);
    CHECK(!empty.InsertFrameLine(Track::Shadow, reason));
    std::cout
        << "Frame-line insertion: stable links, scope, selection, history, bounds and persistence passed\n";
}
