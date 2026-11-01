#include "mikudancestudio/editor_session.hpp"
#include "bone_registration_fixture.hpp"
#include <fstream>
#include <iostream>
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
bool PoseEqual(const PmmBonePose& actual, const PmmEvaluatedBone& expected) {
    return std::equal(expected.translation.begin(), expected.translation.end(),
                      actual.components.begin()) &&
           std::equal(expected.rotation.begin(), expected.rotation.end(),
                      actual.components.begin() + 3);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::uint64_t reusedHash = 14695981039346656037ull;
    for (bool automatic : {false, true})
        for (bool disabled : {false, true}) {
            auto track = bone_registration_fixture::ReuseTrack();
            std::map<std::uint32_t, PmmBoneKey> undo, redo;
            for (unsigned step = 0; step < 7; ++step) {
                CHECK(bone_registration_fixture::ReuseStep(step, automatic, disabled, track, undo,
                                                           redo));
                bone_registration_fixture::HashSlots(reusedHash, track);
            }
        }
    // C7EA0/CDE50/CEF90/CFB80: all 28 tables include the actual free-slot
    // payload, independently compared to the original executable.
    CHECK(reusedHash == 0x7877c69c4e74a379ull);
    const auto root = std::filesystem::absolute(argv[1]);
    const auto writer = [](const auto& path, const auto& bytes, auto&) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return bool(file);
    };
    unsigned cases = 0, distinctPhysicsPoses = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx"})
        for (unsigned mode = 0; mode < 4; ++mode)
            for (bool checked : {false, true}) {
                EditorSession session;
                CHECK(session.RegistrationPhysics());
                CHECK(session.ImportModel(root / file, reason));
                CHECK(session.Edit(
                    "physics-mode",
                    [&](auto& d, auto&) {
                        d.physics.playbackMode = std::uint8_t(mode);
                        return true;
                    },
                    reason));
                CHECK(session.Seek(10, reason));
                CHECK(session.SetBone(0, 0, {2, 3, 4}, {0, 0, 0, 1}, reason));
                PmmEvaluatedBone current, expected, ordinary;
                bool physical = false;
                CHECK(session.Runtime().BoneRegistrationPose(0, 0, 0, current, physical, reason) &&
                      physical);
                CHECK(session.Runtime().BoneRegistrationPose(0, 0, mode, expected, physical,
                                                             reason) &&
                      physical);
                if (expected.translation != current.translation ||
                    expected.rotation != current.rotation)
                    ++distinctPhysicsPoses;
                if (mode == 0)
                    CHECK(expected.translation == current.translation &&
                          expected.rotation == current.rotation);
                CHECK(session.Runtime().BoneRegistrationPose(0, 1, mode, ordinary, physical,
                                                             reason) &&
                      !physical);
                const auto revision = session.Revision(),
                           frames = session.Runtime().Counters().frames;
                const auto* live = &session.Runtime();
                session.SetRegistrationPhysics(checked);
                CHECK(session.RegistrationPhysics() == checked && session.Revision() == revision);
                CHECK(&session.Runtime() == live && session.Runtime().Counters().frames == frames);
                auto untouched = expected;
                physical = false;
                CHECK(!session.Runtime().BoneRegistrationPose(254, 0, mode, untouched, physical,
                                                              reason));
                CHECK(!session.Runtime().BoneRegistrationPose(0, 999, mode, untouched, physical,
                                                              reason));
                CHECK(
                    !session.Runtime().BoneRegistrationPose(0, 0, 4, untouched, physical, reason));
                CHECK(untouched.translation == expected.translation &&
                      untouched.rotation == expected.rotation && !physical);
                CHECK(!session.Register({{Track::Bone, 0, 0, 10}, {Track::Bone, 0, 999, 10}},
                                        reason));
                CHECK(session.Revision() == revision && &session.Runtime() == live);
                CHECK(session.Register({{Track::Bone, 0, 1, 10}, {Track::Bone, 0, 0, 10}}, reason));
                const auto verify = [&] {
                    std::vector<std::vector<PmmBoneKey>> chains;
                    if (!PmmTrackChains(session.Document().models[0].boneKeys, chains))
                        return false;
                    const auto& key = chains[0].back();
                    return key.frame == 10 && key.position == expected.translation &&
                           key.rotation == expected.rotation && key.physicsDisabled == !checked &&
                           chains[1].back().position == ordinary.translation &&
                           chains[1].back().rotation == ordinary.rotation &&
                           !chains[1].back().physicsDisabled;
                };
                CHECK(verify());
                CHECK(PoseEqual(session.Document().models[0].bonePose[0], expected));
                CHECK(session.Document().models[0].bonePose[0].flags[0] == !checked);
                CHECK(!session.Document().models[0].bonePose[0].flags[1]);
                for (unsigned cycle = 0; cycle < 2; ++cycle) {
                    CHECK(session.Undo(reason));
                    CHECK(session.Document().models[0].boneKeys.sparse.empty());
                    CHECK(PoseEqual(session.Document().models[0].bonePose[0], current));
                    CHECK(session.Document().models[0].bonePose[0].flags[1]);
                    CHECK(session.Redo(reason) && verify());
                }
                CHECK(session.Save(root / "registration-physics.pmm", writer, reason));
                CHECK(session.Open(root / "registration-physics.pmm", reason));
                CHECK(session.RegistrationPhysics() == checked && verify());
                CHECK(session.New(reason) && session.RegistrationPhysics() == checked);
                ++cases;
            }
    CHECK(distinctPhysicsPoses > 0);
    // PMM bone order can differ from the resource. Runtime mapping, not the
    // document index interpreted as a rigid-body bone index, chooses physics.
    EditorSession reordered;
    CHECK(reordered.ImportModel(root / "articulated.pmd", reason));
    CHECK(reordered.Edit(
        "reorder-document",
        [&](auto& d, auto&) {
            auto& m = d.models[0];
            std::swap(m.boneNames[0], m.boneNames[1]);
            std::swap(m.bonePose[0], m.bonePose[1]);
            std::swap(m.boneKeys.dense[0], m.boneKeys.dense[1]);
            return true;
        },
        reason));
    PmmEvaluatedBone pose;
    bool physics = false;
    CHECK(reordered.Runtime().BoneRegistrationPose(0, 1, 0, pose, physics, reason) && physics);
    CHECK(reordered.Runtime().BoneRegistrationPose(0, 0, 0, pose, physics, reason) && !physics);
    reordered.SetRegistrationPhysics(false);
    CHECK(reordered.Register({{Track::Bone, 0, 1, 5}, {Track::Bone, 0, 0, 5}}, reason));
    std::vector<std::vector<PmmBoneKey>> chains;
    CHECK(PmmTrackChains(reordered.Document().models[0].boneKeys, chains));
    CHECK(chains[1].back().physicsDisabled && !chains[0].back().physicsDisabled);
    EditorSession reused;
    CHECK(reused.ImportModel(root / "articulated.pmd", reason));
    reused.SetRegistrationPhysics(false);
    CHECK(reused.Register({{Track::Bone, 0, 0, 10}}, reason));
    CHECK(reused.Delete({{Track::Bone, 0, 0, 10}}, reason));
    CHECK(reused.Document().models[0].boneKeys.unused.at(4).physicsDisabled == 1);
    CHECK(reused.Save(root / "free-slot.pmm", writer, reason));
    CHECK(reused.Edit(
        "unrelated-light",
        [&](auto& d, auto&) {
            d.lightState[0] = .5f;
            return true;
        },
        reason));
    CHECK(reused.Document().models[0].boneKeys.unused.at(4).physicsDisabled == 1);
    CHECK(reused.Register({{Track::Bone, 0, 1, 20}}, reason));
    CHECK(reused.Document().models[0].boneKeys.sparse[0].index == 4);
    CHECK(reused.Document().models[0].boneKeys.sparse[0].value.physicsDisabled == 1);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        CHECK(reused.Undo(reason));
        CHECK(reused.Document().models[0].boneKeys.unused.at(4).physicsDisabled == 1);
        CHECK(reused.Redo(reason));
        CHECK(reused.Document().models[0].boneKeys.sparse[0].value.physicsDisabled == 1);
    }
    CHECK(reused.Open(root / "free-slot.pmm", reason));
    CHECK(reused.Document().models[0].boneKeys.unused.empty());
    CHECK(reused.Register({{Track::Bone, 0, 1, 20}}, reason));
    CHECK(!reused.Document().models[0].boneKeys.sparse[0].value.physicsDisabled);
    // Frame-zero merging clears free-slot links and pose but retains physics.
    // Appending this slot after the surviving key must remain a valid chain.
    auto merged = bone_registration_fixture::ReuseTrack();
    merged.dense[0].next = 2;
    PmmBoneKey first{}, last{};
    first.frame = 1;
    first.next = 3;
    first.rotation[3] = 1;
    first.physicsDisabled = 1;
    last.frame = 10;
    last.previous = 2;
    last.rotation[3] = 1;
    merged.sparse = {{2, 0, first}, {3, 0, last}};
    PmmFrameLineDeletion<PmmBoneKey> deletion;
    CHECK(DeletePmmFrameLine(merged, 0, false, deletion));
    CHECK(merged.unused.at(2).next == 0);
    std::map<std::uint32_t, PmmBoneKey> saved;
    CHECK(RegisterPmmBonePose(merged, 0, 20, {}, false, saved));
    CHECK(merged.unused.empty() && merged.sparse[0].value.physicsDisabled == 1);
    for (unsigned invalid : {0, 2, 999999}) {
        auto bad = merged;
        bad.unused[invalid] = {};
        CHECK(!PmmTrackChains(bad, chains));
    }
    auto bad = merged;
    bad.unused[4].frame = 1;
    CHECK(!PmmTrackChains(bad, chains));
    for (const auto kind : {Track::Morph, Track::Display}) {
        EditorSession auxiliary;
        CHECK(auxiliary.ImportModel(root / "triangle.pmx", reason));
        CHECK(auxiliary.ImportModel(root / "triangle.pmx", reason));
        CHECK(auxiliary.SelectModel(0, reason));
        CHECK(auxiliary.Edit(
            "seed-key-marks",
            [&](auto& d, auto&) {
                for (auto& model : d.models) {
                    const auto mark = [](auto& track) {
                        track.dense[0].allocated = 1;
                        auto key = track.dense[0];
                        key.frame = 30;
                        if (!RegisterPmmKey(track, 0, key))
                            return false;
                        key.frame = key.previous = key.next = 0;
                        track.unused[32] = key;
                        return true;
                    };
                    if (!mark(model.boneKeys) || !mark(model.morphKeys) || !mark(model.displayKeys))
                        return false;
                    model.morphPose[0] = .625f;
                    model.selection.loadComplete = 0;
                }
                d.cameraKeys.dense[0].selected = 1;
                return true;
            },
            reason));
        CHECK(auxiliary.SetBone(0, 0, {2, 3, 4}, {0, 0, 0, 1}, reason));
        CHECK(auxiliary.Undo(reason) && auxiliary.CanRedo());
        auxiliary.Selected().keys = {{Track::Bone, 0, 0, 0},
                                     {Track::Morph, 0, 0, 30},
                                     {Track::Display, 0, 0, 30},
                                     {Track::Bone, 1, 0, 0},
                                     {Track::Camera, 255, 0, 0}};
        const auto revision = auxiliary.Revision();
        const auto frames = auxiliary.Runtime().Counters().frames;
        const auto* runtime = &auxiliary.Runtime();
        CHECK(!auxiliary.Register({{kind, 0, 0, 20}, {kind, 254, 0, 20}}, reason));
        CHECK(auxiliary.Revision() == revision && auxiliary.Selected().keys.size() == 5 &&
              auxiliary.CanRedo());
        CHECK(auxiliary.Register({{kind, 0, 0, 20}}, reason));
        CHECK(auxiliary.CanRedo());
        CHECK(auxiliary.Selected().keys.size() == 3);
        CHECK(auxiliary.Selected().keys.count({kind, 0, 0, 20}));
        CHECK(auxiliary.Selected().keys.count({Track::Bone, 1, 0, 0}));
        CHECK(auxiliary.Selected().keys.count({Track::Camera, 255, 0, 0}));
        const auto verifyMarks = [&](const auto& track, Track table) {
            for (const auto& key : track.dense)
                if (key.allocated)
                    return false;
            for (const auto& record : track.sparse)
                if (bool(record.value.allocated) != (table == kind && record.value.frame == 20))
                    return false;
            return !track.unused.at(32).allocated;
        };
        const auto& model = auxiliary.Document().models[0];
        CHECK(verifyMarks(model.boneKeys, Track::Bone));
        CHECK(verifyMarks(model.morphKeys, Track::Morph));
        CHECK(verifyMarks(model.displayKeys, Track::Display));
        const auto& other = auxiliary.Document().models[1];
        CHECK(other.boneKeys.dense[0].allocated && other.morphKeys.dense[0].allocated &&
              other.displayKeys.dense[0].allocated && other.boneKeys.unused.at(32).allocated);
        CHECK(auxiliary.Document().cameraKeys.dense[0].selected);
        CHECK(model.selection.maxFrame == 30 &&
              auxiliary.Document().timeline.lastRegisteredFrame == 30);
        const auto verifyPayload = [&] {
            const auto& m = auxiliary.Document().models[0];
            if (kind == Track::Morph) {
                for (const auto& key : m.morphKeys.sparse)
                    if (key.value.frame == 20)
                        return key.value.value == .625f;
            } else {
                for (const auto& key : m.displayKeys.sparse)
                    if (key.value.frame == 20)
                        return !key.value.visible;
            }
            return false;
        };
        CHECK(verifyPayload());
        CHECK(&auxiliary.Runtime() == runtime && auxiliary.Runtime().Counters().frames == frames);
        CHECK(auxiliary.Redo(reason));
        CHECK(auxiliary.Document().models[0].bonePose[0].components[0] == 2 && verifyPayload());
        CHECK(auxiliary.Save(root / "auxiliary-registration.pmm", writer, reason));
        CHECK(auxiliary.Open(root / "auxiliary-registration.pmm", reason) && verifyPayload());
    }
    EditorSession mixed;
    CHECK(mixed.ImportModel(root / "triangle.pmx", reason));
    CHECK(mixed.Register({{Track::Display, 0, 0, 5},
                          {Track::Morph, 0, 0, 5},
                          {Track::Bone, 0, 0, 5},
                          {Track::Morph, 0, 0, 5}},
                         reason));
    CHECK(mixed.Selected().keys.size() == 3);
    CHECK(mixed.Undo(reason) && !mixed.CanUndo());
    CHECK(mixed.Document().models[0].boneKeys.sparse.empty());
    CHECK(mixed.Document().models[0].morphKeys.sparse.size() == 1 &&
          mixed.Document().models[0].displayKeys.sparse.size() == 1);
    std::cout
        << cases
        << " PMD/PMX physics registration cases, history, persistence and name mapping passed\n";
}
