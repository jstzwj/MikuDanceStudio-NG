#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/bone_pose_names.hpp"
#include <iostream>
#include <fstream>
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
    const auto root = std::filesystem::absolute(argv[1]);
    editor::EditorSession s;
    CHECK(!s.HasBonePoseClipboard());
    const auto emptyRevision = s.Revision();
    CHECK(s.PasteBonePose(false, reason) && s.Revision() == emptyRevision);
    CHECK(s.ImportModel(root / "pose.pmd", reason));
    CHECK(s.SetBone(0, 0, {11, 12, 13}, {0, 0, 0, 1}, reason));
    CHECK(s.SetBone(0, 1, {2, 3, 4}, {.5f, .5f, .5f, .5f}, reason));
    CHECK(s.SetBone(0, 5, {7, 8, 9}, {0, 0, 0, 1}, reason));
    s.Selected().bones = {5, 1, 1};
    const auto revision = s.Revision(), updates = s.Runtime().Counters().frames;
    CHECK(s.CopyBonePose(reason));
    CHECK(s.HasBonePoseClipboard());
    CHECK(s.Revision() == revision && s.Runtime().Counters().frames == updates);
    s.Selected().bones.clear();
    CHECK(s.CopyBonePose(reason) && s.HasBonePoseClipboard());
    CHECK(s.SetBone(0, 1, {20, 30, 40}, {0, 0, 0, 1}, reason));
    CHECK(s.PasteBonePose(true, reason));
    const auto& m = s.Document().models[0];
    CHECK(m.bonePose[0].components[0] == 11);
    CHECK(m.bonePose[1].components[0] == 20); // source is not overwritten
    CHECK(m.bonePose[2].components[0] == -2 && m.bonePose[2].components[1] == 3);
    CHECK(m.bonePose[2].components[3] == .5f && m.bonePose[2].components[4] == -.5f &&
          m.bonePose[2].components[5] == -.5f && m.bonePose[2].components[6] == .5f);
    CHECK(m.bonePose[3].components[0] == 0); // first matching suffix wins
    CHECK(m.bonePose[5].components[0] == -7);
    CHECK(m.bonePose[2].flags[1] == 1 && m.bonePose[2].flags[2] == 1);
    CHECK(m.selection.selectedBone == 5); // clipboard uses model order
    CHECK(s.Selected().bones == (std::vector<std::uint32_t>{2, 5}));
    CHECK(s.Undo(reason) && s.Document().models[0].bonePose[2].components[0] == 0);
    CHECK(s.Redo(reason) && s.Document().models[0].bonePose[2].components[0] == -2);
    s.Selected().bones = {999};
    CHECK(!s.CopyBonePose(reason));
    CHECK(s.ImportModel(root / "pose.pmd", reason));
    CHECK(s.Selected().model == 1);
    CHECK(s.PasteBonePose(false, reason));
    CHECK(s.Document().models[1].bonePose[1].components[0] == 2);
    CHECK(s.Document().models[1].bonePose[5].components[0] == 7);
    CHECK(s.Selected().bones == (std::vector<std::uint32_t>{1, 5}));
    CHECK(s.Document().models[0].bonePose[1].components[0] == 20);
    // Exact source is required for pose mirroring, even if the opposite name exists.
    const std::array<std::string_view, 2> names{"B\x89\x45"
                                                "arm",
                                                "center"};
    CHECK(editor::FindBonePoseTarget(
              "A\x8d\xb6"
              "arm",
              names.size(), [&](std::size_t i) { return names[i]; }, true) == -1);
    CHECK(s.ImportModel(root / "triangle.pmd", reason));
    s.Selected().bones = {0};
    CHECK(s.PasteBonePose(true, reason));
    CHECK(s.Selected().bones.empty());
    CHECK(s.Document().models[2].bonePose[0].components[0] == 0);
    std::cout << "Selected pose copy, exact/mirrored paste, flags and model switching passed\n";
    return 0;
}
