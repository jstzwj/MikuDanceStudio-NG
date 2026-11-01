#pragma once
#include "editor_physics_selection_fixture.hpp"
inline bool PhysicsKeyFixture(mikudancestudio::editor::EditorSession& session,
                              const std::filesystem::path& root, const char* file, bool reverse,
                              unsigned variant, std::string& reason) {
    using namespace mikudancestudio;
    if (!PhysicsSelectionFixture(session, root, file, reverse, variant == 0 ? 0 : 1, reason))
        return false;
    unsigned physical = 0;
    for (unsigned i = 0; i < session.Document().models[0].boneKeys.dense.size(); ++i) {
        int index;
        if (!session.Runtime().DocumentBoneIndex(0, int(i), index, reason))
            return false;
        if (session.Runtime().Resource(0)->EditingBones()[index].hasRigidBody) {
            physical = i;
            break;
        }
    }
    if (!session.Edit(
            "physics-key-detached",
            [&](auto& d, auto&) {
                auto& keys = d.models[0].boneKeys;
                auto detached = keys.dense[physical];
                detached.frame = 17;
                detached.previous = physical;
                detached.next = 0;
                detached.allocated = 1;
                detached.physicsDisabled = 1;
                keys.sparse.push_back({99, 0, detached});
                keys.unused[32].physicsDisabled = 1;
                d.timeline.currentFrame = variant == 0 ? 0 : 3;
                return true;
            },
            reason))
        return false;
    std::set<editor::KeyAddress> selected;
    if (!format::ReadPmmSelectedKeys(session.Document(), 0, selected))
        return false;
    if (variant == 2) {
        for (auto it = selected.begin(); it != selected.end();)
            if (it->track == editor::Track::Bone)
                it = selected.erase(it);
            else
                ++it;
        selected.insert({editor::Track::Bone, 0, physical, 17});
    }
    return session.SetSelectedKeys(selected, reason);
}
