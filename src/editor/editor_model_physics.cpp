#include "mikudancestudio/editor_session.hpp"
namespace mikudancestudio::editor {
bool EditorSession::BeginModelPhysicsEdit(std::uint8_t slot, ModelPhysicsEdit& edit,
                                          std::string& reason) {
    const auto resource = runtime_->Resource(slot);
    if (playing_ || !resource || !resource->PmdSource()) {
        reason = playing_   ? "editor-is-playing"
                 : resource ? "physics-edit-requires-pmd"
                            : "model-not-loaded";
        return false;
    }
    ModelPhysicsEdit next(*resource->PmdSource());
    if (!runtime_->PhysicsEditPose(slot, false, reason))
        return false;
    edit = std::move(next);
    reason.clear();
    return true;
}
bool EditorSession::FinishModelPhysicsEdit(std::uint8_t slot, const ModelPhysicsEdit* edit,
                                           std::string& reason) {
    const auto resource = runtime_->Resource(slot);
    if (playing_ || !resource || !resource->PmdSource()) {
        reason = playing_   ? "editor-is-playing"
                 : resource ? "physics-edit-requires-pmd"
                            : "model-not-loaded";
        return false;
    }
    std::vector<format::RigidData> bodies;
    std::vector<format::JointData> joints;
    if (edit && !edit->Export(bodies, joints, reason))
        return false;
    if (edit) {
        if (!runtime_->ReplacePmdPhysics(slot, bodies, joints, reason))
            return false;
        state_.assets.models[slot] = runtime_->Resource(slot);
        enhancedModelDirty_ = true;
    } else if (!runtime_->PhysicsEditPose(slot, true, reason)) {
        return false;
    }
    ++appearanceRevision_;
    reason.clear();
    return true;
}
}
