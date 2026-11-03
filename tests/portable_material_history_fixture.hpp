#pragma once
#include "mikudancestudio/editor_session.hpp"
inline bool MaterialHistoryScene(mikudancestudio::editor::EditorSession& session,
                                 const std::filesystem::path& root, std::string& reason) {
    return session.ImportModel(root / "uv1-dual.pmx", reason) &&
           session.ImportAccessory(root / "sphere-add.x", reason) &&
           session.Edit(
               "material-history",
               [](auto& d, auto&) {
                   d.view.groundGrid = d.view.groundShadow = 0;
                   d.selfShadowMode = 1;
                   // A paused Qt viewport must select a tool mode to enter
                   // the stock effect path; bone select/box use fixed lighting.
                   d.timeline.editMode = 2;
                   d.physics.accessoryRenderOrder = 0;
                   // The accessory changes the stock effect's spadd after the
                   // model, without contributing pixels to the camera image.
                   d.accessories[0].state.position = {10000, 10000, 10000};
                   d.accessories[0].state.scale = .01f;
                   d.accessories[0].state.shadowEnabled = 1;
                   auto& root = d.accessories[0].keyRecords.dense[0];
                   root.position = d.accessories[0].state.position;
                   root.scale = d.accessories[0].state.scale;
                   root.shadowEnabled = 1;
                   return true;
               },
               reason);
}
