#include "mikudancestudio/editor_session.hpp"
#include "frame_line_deletion_fixture.hpp"
#include "mikudancestudio/pmm_key_deletion.hpp"
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

template <class T> bool DeletionKernel(std::uint64_t& hash) {
    for (unsigned order = 0; order < 3; ++order)
        for (unsigned cursor : {0, 1, 2, 15, 16, 30, 31})
            for (bool automatic : {false, true}) {
                auto track = frame_line_fixture::Make<T>(order);
                PmmFrameLineDeletion<T> deletion;
                if (!DeletePmmFrameLine(track, cursor, automatic, deletion))
                    return false;
                frame_line_fixture::Hash(hash, track);
                if constexpr (std::is_same_v<T, PmmBoneKey>) {
                    if (deletion.affected)
                        for (unsigned cycle = 0; cycle < 3; ++cycle) {
                            std::map<std::uint32_t, T> redo, unused;
                            if (!RestorePmmKeyRecords(track, deletion.captured, redo))
                                return false;
                            frame_line_fixture::Hash(hash, track);
                            if (!RestorePmmKeyRecords(track, redo, unused))
                                return false;
                            frame_line_fixture::Hash(hash, track);
                        }
                }
            }
    return true;
}
bool MarkedBoneKernel(std::uint64_t& hash) {
    for (unsigned order = 0; order < 3; ++order)
        for (bool automatic : {false, true})
            for (const auto& selected : frame_line_fixture::BoneSelections()) {
                auto track = frame_line_fixture::Make<PmmBoneKey>(order);
                for (unsigned i = 0; i < track.dense.size(); ++i)
                    track.dense[i].allocated = selected.count(i) != 0;
                for (auto& key : track.sparse)
                    key.value.allocated = selected.count(unsigned(key.index)) != 0;
                PmmKeyDeletionOptions options;
                options.autoInterpolation = automatic;
                options.physicsBones = {0, 2};
                PmmFrameLineDeletion<PmmBoneKey> deletion;
                if (!DeletePmmKeyRecords(track, selected, options, deletion))
                    return false;
                frame_line_fixture::Hash(hash, track);
                if (!selected.empty())
                    for (unsigned cycle = 0; cycle < 3; ++cycle) {
                        std::map<std::uint32_t, PmmBoneKey> redo, unused;
                        if (!RestorePmmKeyRecords(track, deletion.captured, redo))
                            return false;
                        frame_line_fixture::Hash(hash, track);
                        if (!RestorePmmKeyRecords(track, redo, unused))
                            return false;
                        frame_line_fixture::Hash(hash, track);
                    }
            }
    return true;
}
template <class T> bool MarkedGlobalKernel(std::uint64_t& hash) {
    for (unsigned order = 0; order < 3; ++order)
        for (unsigned shadowMode : {0, 1})
            for (const auto& selected : frame_line_fixture::GlobalSelections()) {
                auto track = frame_line_fixture::Make<T>(order);
                track.dense[0].selected = selected.count(0) != 0;
                for (auto& key : track.sparse)
                    key.value.selected = selected.count(unsigned(key.index)) != 0;
                PmmKeyDeletionOptions options;
                options.shadowMode = std::uint8_t(shadowMode);
                PmmFrameLineDeletion<T> deletion;
                if (!DeletePmmKeyRecords(track, selected, options, deletion))
                    return false;
                frame_line_fixture::Hash(hash, track);
            }
    return true;
}
template <class T> bool MarkedModelAuxKernel(std::uint64_t& hash) {
    const auto& selections = std::is_same_v<T, PmmMorphKey>
                                 ? frame_line_fixture::BoneSelections()
                                 : frame_line_fixture::GlobalSelections();
    for (unsigned order = 0; order < 3; ++order)
        for (const auto& selected : selections) {
            auto track = frame_line_fixture::Make<T>(order);
            for (unsigned i = 0; i < track.dense.size(); ++i)
                track.dense[i].allocated = selected.count(i) != 0;
            for (auto& key : track.sparse)
                key.value.allocated = selected.count(unsigned(key.index)) != 0;
            PmmFrameLineDeletion<T> deletion;
            if (!DeletePmmKeyRecords(track, selected, {}, deletion) || !deletion.captured.empty())
                return false;
            frame_line_fixture::Hash(hash, track);
        }
    return true;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    CHECK(Kernel<PmmBoneKey>() && Kernel<PmmMorphKey>() && Kernel<PmmCameraKey>() &&
          Kernel<PmmLightKey>());
    // Frozen from 384 original x64 deletion/undo/redo persisted-record tables.
    std::uint64_t hash = 14695981039346656037ull;
    CHECK(DeletionKernel<PmmBoneKey>(hash) && DeletionKernel<PmmMorphKey>(hash) &&
          DeletionKernel<PmmCameraKey>(hash) && DeletionKernel<PmmLightKey>(hash));
    CHECK(hash == 0x45bef3b62b79d390ull);
    hash = 14695981039346656037ull;
    CHECK(MarkedBoneKernel(hash) && hash == 0x1f2e2300834a6371ull);
    hash = 14695981039346656037ull;
    CHECK(MarkedGlobalKernel<PmmCameraKey>(hash) && MarkedGlobalKernel<PmmLightKey>(hash) &&
          MarkedGlobalKernel<PmmSelfShadowKey>(hash) && MarkedGlobalKernel<PmmGravityKey>(hash) &&
          MarkedGlobalKernel<PmmAccessoryKey>(hash));
    CHECK(hash == 0xd07a4ddebc56687full);
    hash = 14695981039346656037ull;
    CHECK(MarkedModelAuxKernel<PmmMorphKey>(hash) && MarkedModelAuxKernel<PmmDisplayKey>(hash));
    CHECK(hash == 0x806f058af8b58aa1ull);
    PmmKeyTrack<PmmDisplayKey> display;
    display.dense.resize(1);
    auto& displayRoot = display.dense[0];
    displayRoot.next = 5;
    displayRoot.visible = 0;
    displayRoot.allocated = 1;
    displayRoot.ikEnabled = {0, 1};
    displayRoot.rigidStates = {{4, 7}, {8, 9}};
    auto displayLater = displayRoot;
    displayLater.frame = 20;
    displayLater.next = 0;
    display.sparse.push_back({5, 0, displayLater});
    PmmFrameLineDeletion<PmmDisplayKey> displayDeletion;
    CHECK(DeletePmmKeyRecords(display, {0}, {}, displayDeletion));
    CHECK(display.dense[0].next == 5 && display.dense[0].visible == 1 &&
          display.dense[0].allocated == 0 &&
          display.dense[0].ikEnabled == (std::vector<std::uint8_t>{1, 1}));
    CHECK(display.dense[0].rigidStates[0].modelIndex == -1 &&
          display.dense[0].rigidStates[0].boneIndex == 0);
    CHECK(display.sparse[0].value.visible == 0);
    CHECK(DeletePmmKeyRecords(display, {5}, {}, displayDeletion));
    CHECK(display.sparse.empty() && display.dense[0].next == 0);
    auto malformed = frame_line_fixture::Make<PmmBoneKey>(0);
    malformed.sparse[0].value.previous = 999;
    const auto beforeMalformed = frame_line_fixture::Records(malformed);
    PmmFrameLineDeletion<PmmBoneKey> rejected;
    rejected.affected = 77;
    CHECK(!DeletePmmFrameLine(malformed, 0, true, rejected));
    CHECK(rejected.affected == 77);
    for (const auto& [index, key] : frame_line_fixture::Records(malformed))
        CHECK(frame_line_fixture::Bytes(key) ==
              frame_line_fixture::Bytes(beforeMalformed.at(index)));
    auto invalidRestore = frame_line_fixture::Make<PmmBoneKey>(0);
    auto badKey = invalidRestore.dense[0];
    badKey.next = 999;
    std::map<std::uint32_t, PmmBoneKey> displaced;
    CHECK(!RestorePmmKeyRecords(invalidRestore, {{0, badKey}}, displaced));
    CHECK(invalidRestore.dense[0].next == 13 && displaced.empty());
    CHECK(!DeletePmmKeyRecords(invalidRestore, {0, 999}, {}, rejected));
    CHECK(invalidRestore.dense[0].next == 13 && rejected.affected == 77);
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
    for (auto track : {Track::Bone, Track::Morph, Track::Camera, Track::Light})
        for (unsigned cursor : {0, 1, 15, 31}) {
            EditorSession session;
            session.SetAutoInterpolation(false);
            CHECK(session.ImportModel(root / "triangle.pmx", reason));
            const bool model = track == Track::Bone || track == Track::Morph;
            CHECK(session.SelectModel(model ? 0 : 255, reason));
            const auto slot = session.Selected().model;
            // Reverse allocation order exercises the original partial undo
            // whose root forward chain can bypass a restored predecessor.
            for (unsigned frame : {30, 15, 1}) {
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
                        "light",
                        [&](auto& doc, auto&) {
                            doc.lightState[0] = float(frame) / 100;
                            return true;
                        },
                        reason));
                }
                CHECK(session.Register({{track, slot, 0, frame}}, reason));
            }
            CHECK(session.Seek(cursor, reason));
            CHECK(session.Save(root / "delete-line.pmm", writer, reason));
            CHECK(session.Open(root / "delete-line.pmm", reason));
            session.Selected().keys = {{track, slot, 0, 1}, {track, slot, 0, 30}};
            const auto maximum = session.Document().timeline.lastRegisteredFrame;
            CHECK(session.DeleteFrameLine(track, reason));
            CHECK(session.Document().timeline.lastRegisteredFrame == maximum);
            CHECK(session.Document().timeline.currentFrame == cursor);
            CHECK(session.CanUndo() == (track == Track::Bone && cursor <= 30));
            if (cursor <= 30) {
                CHECK(session.Selected().keys.count({track, slot, 0, 29}));
                if (cursor == 0 && model)
                    CHECK(session.Selected().keys.count({track, slot, 0, 0}));
            }
            if (session.CanUndo()) {
                for (unsigned cycle = 0; cycle < 3; ++cycle) {
                    session.Selected().keys.erase({track, slot, 0, 29});
                    CHECK(session.Undo(reason));
                    CHECK(session.Selected().keys.count({track, slot, 0, 30}));
                    CHECK(session.Document().models[0].boneKeys.sparse.size() == 3);
                    if (cursor == 0) {
                        const auto& keys = session.Document().models[0].boneKeys;
                        CHECK(keys.dense[0].position[0] == 1 && keys.dense[0].next == 2);
                        CHECK(keys.sparse[1].value.previous == 3);
                    }
                    CHECK(session.Redo(reason));
                    CHECK(!session.Selected().keys.count({track, slot, 0, 29}));
                    CHECK(session.Document().models[0].boneKeys.sparse[0].value.allocated == 0);
                }
                if (cursor == 0) {
                    CHECK(session.Undo(reason));
                    CHECK(session.Save(root / "detached-line.pmm", writer, reason));
                    CHECK(session.Open(root / "detached-line.pmm", reason));
                    CHECK(session.Document().models[0].boneKeys.sparse.size() == 3);
                    const auto keys = session.Keys();
                    CHECK(std::any_of(keys.begin(), keys.end(), [&](const auto& key) {
                        return key.track == track && key.slot == slot && key.channel == 0 &&
                               key.frame == 1;
                    }));
                    CHECK(session.Seek(2, reason));
                    CHECK(std::abs(session.Document().models[0].bonePose[0].components[0] - 2) <
                          .005f);
                    CHECK(session.Seek(1, reason));
                    CHECK(session.DeleteFrameLine(track, reason));
                    CHECK(session.Document().models[0].boneKeys.sparse.size() == 2);
                }
            }
            CHECK(session.Save(root / "delete-line.pmm", writer, reason));
            CHECK(session.Open(root / "delete-line.pmm", reason));
            CHECK(session.Play(reason));
            CHECK(!session.DeleteFrameLine(track, reason));
        }
    CHECK(empty.New(reason));
    CHECK(empty.ImportModel(root / "pose.pmd", reason));
    CHECK(empty.Save(root / "empty-delete.pmm", writer, reason));
    const auto emptyRevision = empty.Revision();
    CHECK(empty.DeleteFrameLine(Track::Bone, reason));
    CHECK(empty.Revision() == emptyRevision && !empty.CanUndo());
    CHECK(empty.DeleteFrameLine(Track::Bone, reason, true));
    CHECK(empty.Dirty() && !empty.CanUndo());
    std::cout
        << "Frame-line insertion/deletion: native tables, selection, partial history and persistence passed\n";
}
