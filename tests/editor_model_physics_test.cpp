#include "mikudancestudio/editor_session.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <limits>
#include <cmath>
using namespace mikudancestudio;
using namespace editor;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string&) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(f);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    ModelPhysicsEdit edit;
    CHECK(edit.AddJoint() == -1 && !edit.DeleteBody(0) && !edit.DeleteJoint(0));
    CHECK(edit.AddBody() == 0 && edit.AddBody() == 1 && edit.AddJoint() == 0);
    edit.bodies[0].noCollapse = 5;
    edit.bodies[0].mode = 2;
    edit.bodies[0].friction = .75f;
    edit.bodies[0].restitution = .25f;
    ModelPhysicsClipboard clipboard;
    clipboard.body = edit.bodies[0];
    edit.bodies[1].group = 4;
    edit.bodies[1].noCollapse = 7;
    CHECK(edit.PasteBody(1, clipboard));
    CHECK(edit.bodies[1].group == 4 && edit.bodies[1].noCollapse == 7 &&
          edit.bodies[1].friction == .5f && edit.bodies[1].restitution == 0 &&
          edit.bodies[1].mode == 2);
    edit.joints[0].rigidB = 1;
    CHECK(edit.DeleteBody(0) && edit.jointOrder.empty());
    CHECK(edit.AddBody() == 0 && edit.bodyOrder == (std::vector<int>{1, 0}) &&
          edit.bodies[0].noCollapse == 5);
    CHECK(edit.AddJoint() == 0);
    edit.joints[0].rigidA = 1;
    edit.joints[0].rigidB = 0;
    clipboard.joint = edit.joints[0];
    CHECK(edit.AddJoint() == 1 && edit.PasteJoint(1, clipboard));
    CHECK(std::string(edit.joints[1].pmdName) == "JOINT_1");
    std::vector<format::RigidData> bodies;
    std::vector<format::JointData> joints;
    CHECK(edit.Export(bodies, joints, reason));
    CHECK(bodies.size() == 2 && bodies[0].noCollapse == std::uint16_t(~5) &&
          joints[1].rigidA == 1 && joints[1].rigidB == 0);
    const auto before = bodies;
    edit.joints[1].rigidA = 999;
    CHECK(!edit.Export(bodies, joints, reason) && bodies.size() == before.size());
    CHECK(PhysicsCollisionMask("1 5 16 junk 18 -2 2x 3\t4") ==
          std::uint16_t(1 + 16 + 32768 + 2 + 4));
    CHECK(SetPhysicsBodyValue(bodies[0], 709, -1, true) && bodies[0].size[0] == .1f);
    CHECK(SetPhysicsBodyValue(bodies[0], 718, -1, false) && bodies[0].mass == -1);
    CHECK(SetPhysicsBodyValue(bodies[0], 718, -1, true) && bodies[0].mass == .1f);
    bool clamped = false;
    CHECK(SetPhysicsJointValue(joints[0], 765, 90, clamped) && clamped &&
          joints[0].limits[7] == 80.f / 180.f * 3.141592f);
    CHECK(SetPhysicsJointValue(joints[0], 764, -90, clamped) && clamped &&
          joints[0].limits[10] == -80.f / 180.f * 3.141592f);
    for (unsigned mode : {1u, 2u, 3u}) {
        EditorSession session;
        CHECK(session.ImportModel(root / "articulated.pmd", reason));
        CHECK(session.ImportModel(root / "articulated.pmd", reason));
        session.Selected().model = 0;
        CHECK(session.Edit(
            "mode",
            [&](auto& d, auto&) {
                d.physics.playbackMode = std::uint8_t(mode);
                return true;
            },
            reason));
        CHECK(session.Play(reason));
        for (int i = 0; i < 30; ++i)
            CHECK(session.Advance(1. / 30, reason));
        session.Pause();
        const auto* runtime = &session.Runtime();
        const auto other = session.Runtime().Bodies(1);
        const auto clock = session.Runtime().Simulation();
        const auto count = session.Runtime().Counters().frames;
        const auto revision = session.Revision();
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        const auto originalResource = session.Runtime().Resource(0);
        CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        const auto editedRevision = session.Revision();
        ModelPhysicsEdit cancelled;
        CHECK(session.BeginModelPhysicsEdit(0, cancelled, reason));
        CHECK(session.Document().models[0].bonePose[0].components[0] == 0);
        CHECK(session.Runtime().Bodies(1) == other && &session.Runtime() == runtime &&
              session.Runtime().Counters().frames == count);
        CHECK(session.FinishModelPhysicsEdit(0, nullptr, reason));
        CHECK(session.Runtime().Resource(0) == originalResource && !session.EnhancedModelDirty() &&
              session.Revision() == editedRevision);
        CHECK(session.Runtime().Bodies(1) == other);
        ModelPhysicsEdit modified;
        CHECK(session.BeginModelPhysicsEdit(0, modified, reason));
        const auto neutral = session.Document().models[0].bonePose;
        auto invalid = modified;
        const auto rejected = invalid.AddBody();
        invalid.bodies[rejected].mass = std::numeric_limits<float>::quiet_NaN();
        CHECK(!session.FinishModelPhysicsEdit(0, &invalid, reason));
        CHECK(session.Runtime().Resource(0) == originalResource &&
              session.Runtime().Bodies(1) == other && session.Revision() == editedRevision &&
              session.Document().models[0].bonePose[0].components == neutral[0].components);
        const auto added = modified.AddBody();
        CHECK(added >= 0);
        modified.bodies[added].boneIndex = 0;
        modified.bodies[added].mode = 1;
        modified.bodies[added].position[1] = 3;
        const auto addedJoint = modified.AddJoint();
        CHECK(addedJoint >= 0);
        modified.joints[addedJoint].rigidA = added;
        modified.joints[addedJoint].rigidB = 0;
        CHECK(session.FinishModelPhysicsEdit(0, &modified, reason));
        CHECK(session.EnhancedModelDirty() && session.Revision() == editedRevision);
        CHECK(session.Runtime().Resource(0) != originalResource &&
              session.Assets().models.at(0) == session.Runtime().Resource(0));
        CHECK(session.Runtime().Resource(1) && session.Runtime().Bodies(1) == other &&
              &session.Runtime() == runtime);
        CHECK(session.Runtime().Counters().frames == count &&
              session.Runtime().Simulation().remainingSeconds == clock.remainingSeconds &&
              session.Runtime().Simulation().cursorSeconds == clock.cursorSeconds &&
              session.Runtime().Simulation().resetPending == clock.resetPending);
        CHECK(session.Runtime().Resource(0)->PmdSource()->rigids.size() ==
              originalResource->PmdSource()->rigids.size() + 1);
        const auto enhancedResource = session.Runtime().Resource(0);
        CHECK(session.CanUndo() && session.Undo(reason));
        CHECK(session.Runtime().Resource(0) == enhancedResource && session.Redo(reason));
        CHECK(session.Runtime().Resource(0) == enhancedResource);
        CHECK(session.SaveEnhancedModel(0, root / "physics-save.pmd", Write, reason));
        EditorSession reopened;
        CHECK(reopened.ImportModel(root / "physics-save.pmd", reason));
        CHECK(reopened.Runtime().Resource(0)->PmdSource()->rigids.size() ==
              modified.bodyOrder.size());
        CHECK(session.Play(reason));
        for (int i = 0; i < 120; ++i)
            CHECK(session.Advance(1. / 30, reason));
        session.Pause();
        CHECK(session.Runtime().Bodies(0).size() == modified.bodyOrder.size());
        CHECK(session.UnloadModel(0, reason));
        CHECK(!session.BeginModelPhysicsEdit(0, modified, reason));
        CHECK(session.ImportModel(root / "triangle.pmx", reason));
        CHECK(!session.BeginModelPhysicsEdit(0, modified, reason));
        CHECK(revision != editedRevision && !redo);
        (void)undo;
    }
    EditorSession clipboardSession;
    {
        EditorSession session;
        CHECK(session.ImportModel(root / "articulated.pmd", reason));
        CHECK(session.Play(reason));
        for (unsigned i = 0; i < 30; ++i)
            CHECK(session.Advance(1. / 30, reason));
        session.Pause();
        const auto before = session.Runtime().Bodies(0);
        ModelPhysicsEdit edit;
        CHECK(session.BeginModelPhysicsEdit(0, edit, reason));
        for (unsigned i = 0; i < 60; ++i)
            CHECK(session.TickIdle(1. / 30, reason));
        CHECK(session.Runtime().Bodies(0) == before);
        CHECK(session.FinishModelPhysicsEdit(0, nullptr, reason));
        CHECK(session.TickIdle(1. / 30, reason));
        CHECK(session.Runtime().Bodies(0) != before);
    }
    // The native Add Joint default binds both ends to scratch body zero.
    // This is a valid dialog result, not a missing/out-of-range reference.
    for (unsigned mode : {0u,1u,2u}) {
        EditorSession session;
        CHECK(session.ImportModel(root / "triangle.pmd", reason));
        ModelPhysicsEdit edit;
        CHECK(session.BeginModelPhysicsEdit(0,edit,reason));
        const auto body = edit.AddBody();
        edit.bodies[body].mode = std::uint8_t(mode);
        const auto joint = edit.AddJoint();
        CHECK(edit.joints[joint].rigidA == body && edit.joints[joint].rigidB == body);
        CHECK(session.FinishModelPhysicsEdit(0,&edit,reason));
        CHECK(session.Runtime().Resource(0)->PmdSource()->joints.size()==1);
        CHECK(session.Play(reason));
        for (unsigned frame=0;frame<120;++frame)
            CHECK(session.Advance(1./30,reason));
        session.Pause();
        for (const auto& record : session.Runtime().Bodies(0))
            for (float value : record)
                CHECK(std::isfinite(value));
        CHECK(session.SaveEnhancedModel(0,root/"self-joint.pmd",Write,reason));
        EditorSession reopened;
        CHECK(reopened.ImportModel(root/"self-joint.pmd",reason));
        CHECK(reopened.UnloadModel(0,reason));
    }
    clipboardSession.PhysicsClipboard().body.mass = 17;
    CHECK(clipboardSession.New(reason) && clipboardSession.PhysicsClipboard().body.mass == 17);
    std::cout
        << "Physics staging, selected-owner replacement, saved PMD and retained shared world passed\n";
}
