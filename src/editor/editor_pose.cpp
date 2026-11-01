#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/bone_pose_names.hpp"
#include <algorithm>

namespace mikudancestudio::editor {
bool EditorSession::CopyBonePose(std::string& reason) {
    const auto& document = Document();
    const auto model = std::find_if(document.models.begin(), document.models.end(),
                                    [&](const auto& m) { return m.slot == selection_.model; });
    if (model == document.models.end()) {
        reason = "pose-model-unavailable";
        return false;
    }
    if (selection_.bones.empty()) {
        reason.clear();
        return true; // Native no-selection copy retains the previous clipboard.
    }
    const std::set<std::uint32_t> selected(selection_.bones.begin(), selection_.bones.end());
    if (*selected.rbegin() >= model->bonePose.size()) {
        reason = "bone-selection-expired";
        return false;
    }
    std::vector<CopiedBonePose> next;
    for (std::size_t i = 0; i < model->bonePose.size(); ++i)
        if (selected.count(std::uint32_t(i))) {
            CopiedBonePose copy;
            copy.name = model->boneNames[i];
            std::copy_n(model->bonePose[i].components.begin(), 3, copy.position.begin());
            std::copy_n(model->bonePose[i].components.begin() + 3, 4, copy.rotation.begin());
            next.push_back(std::move(copy));
        }
    poseClipboard_ = std::move(next);
    reason.clear();
    return true;
}

bool EditorSession::PasteBonePose(bool mirrored, std::string& reason) {
    if (poseClipboard_.empty()) {
        reason.clear();
        return true;
    }
    auto next = Current();
    auto model = std::find_if(next.document.models.begin(), next.document.models.end(),
                              [&](const auto& m) { return m.slot == selection_.model; });
    if (model == next.document.models.end()) {
        reason = "pose-model-unavailable";
        return false;
    }
    next.selection.bones.clear();
    for (auto& pose : model->bonePose)
        pose.flags[2] = 0;
    const auto nameAt = [&](std::size_t i) {
        const auto& name = model->boneNames[i];
        return std::string_view(name.data(), name.size());
    };
    for (const auto& copied : poseClipboard_) {
        const auto index = FindBonePoseTarget({copied.name.data(), copied.name.size()},
                                              model->boneNames.size(), nameAt, mirrored);
        if (index < 0)
            continue;
        auto& pose = model->bonePose[std::size_t(index)];
        std::copy(copied.position.begin(), copied.position.end(), pose.components.begin());
        std::copy(copied.rotation.begin(), copied.rotation.end(), pose.components.begin() + 3);
        if (mirrored) {
            pose.components[0] = -pose.components[0];
            pose.components[4] = -pose.components[4];
            pose.components[5] = -pose.components[5];
        }
        pose.flags[1] = pose.flags[2] = 1;
        model->selection.selectedBone = index;
    }
    for (std::size_t i = 0; i < model->bonePose.size(); ++i)
        if (model->bonePose[i].flags[2])
            next.selection.bones.push_back(std::uint32_t(i));
    return Commit(std::move(next), reason);
}
}
