#pragma once
#include "mikudancestudio/editor_session.hpp"

inline bool PhysicsSelectionFixture(mikudancestudio::editor::EditorSession& editor,
                                    const std::filesystem::path& root, const char* file,
                                    bool reverse, unsigned mode, std::string& reason) {
    using namespace mikudancestudio;
    if (!editor.New(reason) || !editor.ImportModel(root / file, reason) ||
        !editor.ImportModel(root / "triangle.pmx", reason) || !editor.SelectModel(0, reason))
        return false;
    if (reverse && !editor.Edit(
                       "reorder-physics-channels",
                       [](auto& d, auto&) {
                           auto& model = d.models[0];
                           std::reverse(model.boneNames.begin(), model.boneNames.end());
                           std::reverse(model.bonePose.begin(), model.bonePose.end());
                           std::reverse(model.boneKeys.dense.begin(), model.boneKeys.dense.end());
                           for (auto& index : model.ikSources)
                               index = int(model.boneNames.size()) - 1 - index;
                           if (model.selection.selectedBone >= 0)
                               model.selection.selectedBone =
                                   int(model.boneNames.size()) - 1 - model.selection.selectedBone;
                           return true;
                       },
                       reason))
        return false;
    const auto count = unsigned(editor.Document().models[0].bonePose.size());
    if (mode != 0) {
        std::vector<editor::KeyAddress> keys;
        for (unsigned bone = 0; bone < count; ++bone)
            for (unsigned frame : {2, 5})
                keys.push_back({editor::Track::Bone, 0, bone, frame});
        if (!editor.Register(keys, reason))
            return false;
    }
    if (!editor.Edit(
            "physics-selection-payload",
            [&](auto& d, auto&) {
                auto& model = d.models[0];
                for (auto& key : model.boneKeys.dense) {
                    key.physicsDisabled = mode == 2;
                    key.allocated = 1;
                }
                for (auto& key : model.boneKeys.sparse) {
                    key.value.physicsDisabled = key.value.frame == 2;
                    key.value.allocated = 1;
                }
                const auto markUnused = [](auto& track) {
                    for (auto& key : track.dense)
                        key.allocated = 1;
                    track.unused[32] = {};
                    track.unused[32].allocated = 1;
                };
                markUnused(model.boneKeys);
                markUnused(model.morphKeys);
                markUnused(model.displayKeys);
                d.models[1].boneKeys.dense[0].allocated = 1;
                return true;
            },
            reason))
        return false;
    editor.Selected().keys.clear();
    for (const auto& key : editor.Keys())
        editor.Selected().keys.insert(key);
    return true;
}
