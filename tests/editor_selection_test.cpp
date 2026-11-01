#include "mikudancestudio/editor_session.hpp"
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
int main(int argc, char** argv) {
    CHECK(argc == 2);
    {
        struct Bone {
            cpu_bone::BoneType type = cpu_bone::BoneType::RotateMove;
            std::uint16_t flags = cpu_bone::kBoneFlagVisible;
            bool hasRigidBody = false, physicsDisabled = false;
            int screenX = 100, screenY = 100;
        } bone;
        std::uint8_t selected = 0;
        int active = -1;
        const auto pick = [&](int mode) {
            return cpu_bone::Pick(
                1, [&](auto) -> const auto& { return bone; },
                [&](auto) -> auto& { return selected; }, active, 100, 100, mode, false);
        };
        for (unsigned type = 0; type < 256; ++type) {
            bone.type = static_cast<cpu_bone::BoneType>(type);
            const bool eligible = type < 7 || type == 8 || type >= 128;
            CHECK(pick(0) == eligible);
        }
        bone.type = cpu_bone::BoneType::Move;
        bone.hasRigidBody = true;
        CHECK(pick(0) && !pick(1) && !pick(2) && pick(3));
        bone.physicsDisabled = true;
        CHECK(!pick(1) && pick(2));
        bone.flags = 0;
        CHECK(!pick(0));
        bone.type = cpu_bone::BoneType::FixedAxis;
        cpu_bone::Box(
            1, [&](auto) -> const auto& { return bone; }, [&](auto) -> auto& { return selected; },
            99, 99, 101, 101, false);
        CHECK(selected); // Box includes hidden and physics-driven bones.
    }
    editor::EditorSession session;
    const auto root = std::filesystem::absolute(argv[1]);
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    const auto revision = session.Revision(), updates = session.Runtime().Counters().frames;
    const auto canUndo = session.CanUndo();
    std::vector<cpu_bone::ScreenPoint> points(8);
    for (unsigned i = 0; i < points.size(); ++i)
        points[i] = {100 + int(i) * 30, 100};
    CHECK(session.SelectBones({}, -1, reason));
    CHECK(session.PickBone(points, 107, 100, false, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{0}));
    CHECK(session.Document().models[0].selection.selectedBone == 0);
    CHECK(
        session.PickBone(points, 138, 100, false, reason)); // strict radius; miss retains selection
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{0}));
    points[1] = points[0];
    for (int expected : {1, 0, 1}) {
        CHECK(session.PickBone(points, 100, 100, false, reason));
        CHECK(session.Selected().bones == (std::vector<std::uint32_t>{unsigned(expected)}));
        CHECK(session.Document().models[0].selection.selectedBone == expected);
    }
    CHECK(session.SelectBones({}, -1, reason));
    CHECK(session.PickBone(points, 100, 100, true, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{0, 1}));
    CHECK(session.PickBone(points, 100, 100, true, reason));
    CHECK(session.Selected().bones.empty());
    editor::EditorSession physics;
    CHECK(physics.ImportModel(root / "articulated.pmd", reason));
    const auto info = physics.Runtime().Resource(0)->EditingBones();
    CHECK(
        std::any_of(info.begin(), info.end(), [](const auto& bone) { return bone.hasRigidBody; }));
    CHECK(session.Document().models[0].selection.selectedBone == -1);
    CHECK(session.BoxSelectBones(points, 99, 99, 161, 101, false, false, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{0, 1, 2}));
    CHECK(session.Document().models[0].selection.selectedBone == -1);
    CHECK(session.BoxSelectBones(points, 99, 99, 161, 101, false, true, reason));
    CHECK(session.Document().models[0].selection.selectedBone == 0);
    CHECK(session.BoxSelectBones(points, 100, 99, 161, 101, false, true, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{2}));
    CHECK(session.Document().models[0].selection.selectedBone == 2);
    CHECK(session.BoxSelectBones(points, 9, 9, 11, 11, true, true, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{2}));
    CHECK(session.Revision() == revision && session.Runtime().Counters().frames == updates);
    CHECK(session.CanUndo() == canUndo);
    CHECK(!session.PickBone({}, 0, 0, false, reason));
    CHECK(!session.SelectBones({8}, 8, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{2}));
    CHECK(session.SelectModel(255, reason));
    CHECK(session.SelectModel(0, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{2}));
    CHECK(session.SetBone(0, 3, {1, 0, 0}, {0, 0, 0, 1}, reason));
    const auto editedUpdates = session.Runtime().Counters().frames;
    session.SetEditMode(4);
    CHECK(session.SelectAllBones(true, reason));
    CHECK(session.Selected().bones == (std::vector<std::uint32_t>{3}));
    CHECK(session.Document().models[0].selection.selectedBone == -1);
    CHECK(session.Document().timeline.editMode == 0);
    CHECK(session.Runtime().Counters().frames == editedUpdates);
    CHECK(session.SelectAllBones(false, reason));
    CHECK(session.Selected().bones.size() == 8);
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.Selected().bones.empty());
    CHECK(session.SelectModel(0, reason));
    CHECK(session.Selected().bones.size() == 8);
    CHECK(session.SelectModel(1, reason));
    CHECK(session.Selected().bones.empty());
    std::cout
        << "Bone selection: click/cycle, Shift, box, markers, model isolation and no evaluation passed\n";
}
