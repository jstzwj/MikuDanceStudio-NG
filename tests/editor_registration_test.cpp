#include "mikudancestudio/editor_session.hpp"
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
    std::cout
        << cases
        << " PMD/PMX physics registration cases, history, persistence and name mapping passed\n";
}
