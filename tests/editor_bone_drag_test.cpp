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
bool Near(float a, float b) {
    return std::abs(a - b) < 2e-6f;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    editor::EditorSession s;
    editor::BoneDragInput drag;
    drag.mode = 10;
    drag.viewWidth = 640;
    drag.viewHeight = 480;
    drag.y = -10;
    CHECK(!s.DragBones(drag, reason));
    CHECK(s.ImportModel(root / "drag.pmd", reason));
    CHECK(s.SelectBones({0, 1}, 0, reason));
    CHECK(!s.CanUndo());
    CHECK(s.BeginBoneDrag(1, reason));
    CHECK(s.Document().timeline.editMode == 5 && s.CanUndo());
    s.EndBoneDrag();
    CHECK(s.Document().timeline.editMode == 3);
    CHECK(s.Undo(reason) && !s.CanUndo());
    CHECK(s.BeginGesture());
    CHECK(s.DragBones(drag, reason));
    CHECK(Near(s.Document().models[0].bonePose[0].components[0], .5f));
    CHECK(s.Document().models[0].bonePose[1].components[0] == 0); // inherits selected parent
    CHECK(s.Document().models[0].bonePose[0].flags[1] == 1);
    drag.y = -20;
    drag.previousY = -10;
    CHECK(s.DragBones(drag, reason));
    CHECK(Near(s.Document().models[0].bonePose[0].components[0], 1));
    s.EndGesture();
    CHECK(s.Undo(reason));
    CHECK(s.Document().models[0].bonePose[0].components[0] == 0 && !s.CanUndo());
    CHECK(s.Redo(reason));
    CHECK(Near(s.Document().models[0].bonePose[0].components[0], 1));
    CHECK(s.Undo(reason));
    drag.previousY = 0;
    drag.y = -10;
    drag.mode = 4;
    CHECK(s.DragBones(drag, reason));
    const auto q = s.Document().models[0].bonePose[0].components;
    CHECK(Near(q[3], std::sin(.05f)) && Near(q[6], std::cos(.05f))); // native FULL angle
    CHECK(s.Undo(reason));
    drag.shift = true;
    CHECK(s.DragBones(drag, reason));
    CHECK(Near(s.Document().models[0].bonePose[0].components[3], std::sin(.25f)));
    CHECK(s.Undo(reason));
    drag.shift = false;
    drag.control = true;
    CHECK(s.DragBones(drag, reason));
    CHECK(Near(s.Document().models[0].bonePose[0].components[3], std::sin(.005f)));
    CHECK(s.Undo(reason));
    drag.control = false;
    drag.mode = 10;
    drag.clipW = -1; // Axis movement does not consume clip depth.
    CHECK(s.DragBones(drag, reason));
    CHECK(Near(s.Document().models[0].bonePose[0].components[0], .5f));
    CHECK(s.Undo(reason));
    drag.clipW = 1;
    // A child under a quarter-turn parent converts world X into its parent-space -Y.
    CHECK(s.SetBone(0, 0, {0, 0, 0}, {0, 0, std::sqrt(.5f), std::sqrt(.5f)}, reason));
    CHECK(s.SelectBones({1}, 1, reason));
    s.CycleCoordinateSystem(); // world axes
    drag.mode = 10;
    CHECK(s.DragBones(drag, reason));
    const auto& child = s.Document().models[0].bonePose[1].components;
    CHECK(Near(child[0], 0) && Near(child[1], -.5f));
    CHECK(s.Undo(reason));
    const auto rev = s.Revision(), frames = s.Runtime().Counters().frames;
    drag.clipW = std::numeric_limits<float>::quiet_NaN();
    CHECK(!s.DragBones(drag, reason));
    CHECK(s.Revision() == rev && s.Runtime().Counters().frames == frames);
    drag.clipW = 1;
    drag.x = std::numeric_limits<int>::min();
    CHECK(!s.DragBones(drag, reason) && s.Revision() == rev);
    drag.x = 0;
    CHECK(s.Play(reason));
    CHECK(!s.DragBones(drag, reason));
    s.Pause();
    CHECK(s.BeginBoneDrag(1, reason));
    CHECK(s.Seek(0, reason));
    CHECK(s.BoneDragMode() == 0 && s.Document().timeline.editMode == 3);
    s.EndBoneDrag();
    // Each mode is executable for PMD and PMX, and undo restores the exact live pose.
    for (const auto* name : {"drag.pmd", "triangle.pmx", "articulated.pmd", "articulated.pmx"}) {
        editor::EditorSession trial;
        CHECK(trial.ImportModel(root / name, reason));
        CHECK(trial.SelectBones({0}, 0, reason));
        for (int mode = 1; mode <= 12; ++mode) {
            drag.mode = mode;
            drag.x = 7;
            drag.y = -9;
            drag.previousX = drag.previousY = 0;
            const auto before = trial.Document().models[0].bonePose[0].components;
            CHECK(trial.BeginBoneDrag(unsigned(mode), reason));
            CHECK(trial.DragBones(drag, reason));
            trial.EndBoneDrag();
            CHECK(trial.Undo(reason));
            CHECK(trial.Document().models[0].bonePose[0].components == before);
        }
    }
    std::cout
        << "Bone drag: native angle, precision, parent space, selected roots, gesture undo, failure atomicity passed\n";
}
