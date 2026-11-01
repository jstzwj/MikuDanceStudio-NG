#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/bone_pose_names.hpp"
#include "mikudancestudio/bone_pose_reset.hpp"
#include "mikudancestudio/text_encoding.hpp"
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
    HistoryEntry history;
    history.frame = std::uint32_t(next.document.timeline.currentFrame);
    for (const auto& copied : poseClipboard_) {
        const auto index = FindBonePoseTarget({copied.name.data(), copied.name.size()},
                                              model->boneNames.size(), nameAt, mirrored);
        if (index < 0) {
            // Original allocates one zeroed undo record per clipboard item,
            // including unmatched names. Preserve that observable behavior.
            history.poses.emplace_back();
            continue;
        }
        auto& pose = model->bonePose[std::size_t(index)];
        auto displaced =
            CaptureHistory(*model, HistoryKind::Pose, history.frame, {std::uint32_t(index)});
        history.poses.push_back(displaced.poses.front());
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
    return Commit(std::move(next), reason, {{selection_.model, history}});
}

bool EditorSession::ResetSelectedBonePose(std::string& reason) {
    if (selection_.bones.empty()) {
        reason.clear();
        return true;
    }
    auto next = Current();
    auto model = std::find_if(next.document.models.begin(), next.document.models.end(),
                              [&](const auto& m) { return m.slot == selection_.model; });
    const auto resource = next.assets.models.find(selection_.model);
    if (model == next.document.models.end() || resource == next.assets.models.end()) {
        reason = "pose-model-unavailable";
        return false;
    }
    const std::set<std::uint32_t> selected(selection_.bones.begin(), selection_.bones.end());
    if (*selected.rbegin() >= model->bonePose.size()) {
        reason = "bone-selection-expired";
        return false;
    }
    const auto history = CaptureHistory(*model, HistoryKind::Pose,
                                        std::uint32_t(next.document.timeline.currentFrame),
                                        {selected.begin(), selected.end()});
    const auto& names = resource->second->BoneNames();
    for (auto index : selected) {
        std::string name;
        const auto& encoded = model->boneNames[index];
        if (!text::DecodeCp932({encoded.data(), encoded.size()}, name)) {
            reason = "invalid-bone-name";
            return false;
        }
        const auto found = std::find(names.begin(), names.end(), name);
        if (found == names.end()) {
            reason = "bone-name-not-bound";
            return false;
        }
        const auto& bone =
            resource->second->Document().bones[std::size_t(found - names.begin())].fields;
        auto& pose = model->bonePose[index];
        const auto& root = model->bonePose[0].components;
        const auto matrix = cpu_math::RotationQuaternion(
            {pose.components[3], pose.components[4], pose.components[5], pose.components[6]});
        ResetBonePose(index, bone.parent, bone.position, matrix.data(), root.data(),
                      root.data() + 3, pose.components.data(), pose.components.data() + 3,
                      pose.flags[1]);
    }
    return Commit(std::move(next), reason, {{selection_.model, history}});
}
}
