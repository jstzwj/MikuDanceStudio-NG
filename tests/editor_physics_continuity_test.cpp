#include "mikudancestudio/editor_session.hpp"
#include <iostream>
#include <limits>
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
bool SameClock(const CpuSimulationState& a, const CpuSimulationState& b) {
    return a.cursorSeconds == b.cursorSeconds && a.remainingSeconds == b.remainingSeconds &&
           a.physicsMode == b.physicsMode && a.resetPending == b.resetPending &&
           a.bodiesMoved == b.bodiesMoved && a.gravityNoiseTimer == b.gravityNoiseTimer;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    for (const char* file : {"articulated.pmd", "articulated.pmx"}) {
        EditorSession editor;
        CHECK(editor.ImportModel(root / file, reason));
        CHECK(editor.Edit(
            "running physics",
            [](auto& d, auto&) {
                d.physics.playbackMode = 2;
                d.physics.gravityNoiseEnabled = 1;
                d.physics.gravityNoise = 20;
                return true;
            },
            reason));
        CHECK(editor.Play(reason));
        for (unsigned step = 0; step < 8; ++step)
            CHECK(editor.Advance(1. / 30, reason));
        editor.Pause();
        const auto* live = &editor.Runtime();
        const auto bodies = live->Bodies(0);
        const auto clock = live->Simulation();
        const auto frames = live->Counters().frames;
        auto updates = live->Counters().editorUpdates;
        CHECK(!bodies.empty());
        const auto retained = [&] {
            return &editor.Runtime() == live && editor.Runtime().Bodies(0) == bodies &&
                   editor.Runtime().Counters().frames == frames &&
                   SameClock(editor.Runtime().Simulation(), clock);
        };
        CHECK(editor.Edit(
            "light",
            [](auto& d, auto&) {
                d.lightState[0] = .25f;
                return true;
            },
            reason));
        CHECK(retained() && live->Counters().editorUpdates == ++updates);
        CHECK(editor.SetBone(0, 3, {.5f, 1, 0}, {0, 0, 0, 1}, reason));
        CHECK(retained() && live->Counters().editorUpdates == ++updates);
        CHECK(editor.Document().models[0].bonePose[3].components[0] == .5f);
        const auto frame = unsigned(editor.Document().timeline.currentFrame);
        CHECK(editor.Register({{Track::Bone, 0, 3, frame}}, reason));
        CHECK(retained() && live->Counters().editorUpdates == ++updates);
        CHECK(editor.Copy({{Track::Bone, 0, 3, frame}}, reason));
        CHECK(editor.Paste(frame + 10, false, reason));
        CHECK(retained() && live->Counters().editorUpdates == ++updates);
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            CHECK(editor.Undo(reason));
            CHECK(retained() && live->Counters().editorUpdates == ++updates);
            CHECK(editor.Redo(reason));
            CHECK(retained() && live->Counters().editorUpdates == ++updates);
        }
        const auto revision = editor.Revision();
        const auto position = editor.Runtime().Snapshot(0)->vertices[0].position;
        CHECK(!editor.Edit(
            "invalid parent",
            [](auto& d, auto&) {
                d.cameraKeys.dense[0].parentModel = 200;
                return true;
            },
            reason));
        CHECK(retained() && editor.Revision() == revision &&
              live->Counters().editorUpdates == updates);
        CHECK(!editor.Edit(
            "overflowing output",
            [](auto& d, auto&) {
                d.models[0].bonePose[3].components[0] = std::numeric_limits<float>::max();
                d.models[0].bonePose[3].components[1] = std::numeric_limits<float>::max();
                d.models[0].bonePose[3].components[3] = std::numeric_limits<float>::max();
                d.models[0].bonePose[3].components[4] = std::numeric_limits<float>::max();
                return true;
            },
            reason));
        CHECK(retained() && editor.Revision() == revision &&
              live->Counters().editorUpdates == updates);
        CHECK(editor.Runtime().Snapshot(0)->vertices[0].position == position);
        CHECK(editor.Undo(reason)); // Failed commands did not consume history.
        CHECK(retained());

        // Keep two independent worlds in lockstep. Metadata edits must retain
        // contacts, solver state, velocities and the deterministic noise clock,
        // as observed by subsequent steps, not just the immediate transforms.
        SceneSession session;
        CHECK(SceneSession::Create(editor.Document(), session, reason));
        runtime::SceneRuntime edited, control;
        CHECK(edited.LoadResources(session, editor.Assets().models, reason, true));
        CHECK(control.LoadResources(session, editor.Assets().models, reason, true));
        for (unsigned step = 0; step < 20; ++step) {
            CHECK(edited.Tick(1.f / 60, reason) && control.Tick(1.f / 60, reason));
            CHECK(edited.Bodies(0) == control.Bodies(0));
        }
        auto doc = edited.Session()->Document();
        doc.lightState[1] = .75f;
        CHECK(SceneSession::Create(doc, session, reason));
        CHECK(edited.ApplyEditorDocument(std::move(session), reason));
        CHECK(edited.Bodies(0) == control.Bodies(0));
        for (unsigned step = 0; step < 20; ++step) {
            CHECK(edited.Tick(1.f / 60, reason) && control.Tick(1.f / 60, reason));
            CHECK(edited.Bodies(0) == control.Bodies(0) &&
                  SameClock(edited.Simulation(), control.Simulation()));
        }
    }
    std::cout << "PMD/PMX edit/history physics continuity and atomic rejection passed\n";
}
