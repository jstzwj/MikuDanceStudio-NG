#include "mikudancestudio/editor_session.hpp"
#include <cmath>
namespace mikudancestudio::editor {
bool EditorSession::EffectAddressValid(EffectAddress a) const {
    if (a.slot == 255 || a.subset < -1)
        return false;
    if (a.accessory) {
        const auto it = Assets().accessories.find(a.slot);
        return it != Assets().accessories.end() &&
               (a.subset == -1 || unsigned(a.subset) < it->second->Subsets().size());
    }
    const auto it = Assets().models.find(a.slot);
    return it != Assets().models.end() &&
           (a.subset == -1 || unsigned(a.subset) < it->second->Document().materials.size());
}
void EditorSession::ChangeEffects(EffectDocument next) {
    effectUndo_.push_back(effects_);
    if (effectUndo_.size() > 128)
        effectUndo_.erase(effectUndo_.begin());
    effectRedo_.clear();
    effects_ = std::move(next);
    ++effectRevision_;
    revision_ = nextRevision_++;
}
void EditorSession::PruneEffects() {
    bool changed = false;
    const auto prune = [&](EffectDocument& doc) {
        for (auto it = doc.assignments.begin(); it != doc.assignments.end();)
            if (!EffectAddressValid(it->first)) {
                it = doc.assignments.erase(it);
                changed = true;
            } else
                ++it;
    };
    prune(effects_);
    for (auto& doc : effectUndo_)
        prune(doc);
    for (auto& doc : effectRedo_)
        prune(doc);
    if (changed)
        ++effectRevision_;
}
bool EditorSession::AssignEffect(EffectAddress a, const std::filesystem::path& source,
                                 std::string& reason) {
    if (!EffectAddressValid(a)) {
        reason = "effect-address-expired";
        return false;
    }
    auto next = effects_;
    if (source.empty())
        next.assignments.erase(a);
    else {
        const auto path = std::filesystem::absolute(source).lexically_normal();
        if (path.extension() != ".fx" && path.extension() != ".FX") {
            reason = "effect-source-extension";
            return false;
        }
        next.assignments[a] = {path};
    }
    ChangeEffects(std::move(next));
    reason.clear();
    return true;
}
bool EditorSession::EnableEffect(EffectAddress a, bool enabled, std::string& reason) {
    auto next = effects_;
    auto it = next.assignments.find(a);
    if (!EffectAddressValid(a) || it == next.assignments.end()) {
        reason = "effect-assignment-missing";
        return false;
    }
    it->second.enabled = enabled;
    ChangeEffects(std::move(next));
    reason.clear();
    return true;
}
bool EditorSession::ReloadEffect(EffectAddress a, std::string& reason) {
    auto next = effects_;
    auto it = next.assignments.find(a);
    if (!EffectAddressValid(a) || it == next.assignments.end() || it->second.reload == UINT64_MAX) {
        reason = "effect-assignment-missing";
        return false;
    }
    ++it->second.reload;
    // Reload reads external source again; it does not change persisted project
    // settings and cannot undo an edit made in an external text editor.
    effects_ = std::move(next);
    ++effectRevision_;
    reason.clear();
    return true;
}
bool EditorSession::SetEffectParameter(EffectAddress a, const std::string& name,
                                       const mme::ParameterValue& value, std::string& reason) {
    auto next = effects_;
    auto it = next.assignments.find(a);
    if (!EffectAddressValid(a) || it == next.assignments.end()) {
        reason = "effect-assignment-missing";
        return false;
    }
    if (!ValidEffectParameter(name, value)) {
        reason = "effect-parameter-shape";
        return false;
    }
    it->second.parameters[name] = value;
    ChangeEffects(std::move(next));
    reason.clear();
    return true;
}
void EditorSession::SetBasicMaterials(bool basic) {
    if (effects_.basicMaterials == basic)
        return;
    auto next = effects_;
    next.basicMaterials = basic;
    ChangeEffects(std::move(next));
}
bool EditorSession::UndoEffects(std::string& reason) {
    if (effectUndo_.empty()) {
        reason = "effect-undo-empty";
        return false;
    }
    effectRedo_.push_back(effects_);
    effects_ = std::move(effectUndo_.back());
    effectUndo_.pop_back();
    PruneEffects();
    ++effectRevision_;
    revision_ = nextRevision_++;
    reason.clear();
    return true;
}
bool EditorSession::RedoEffects(std::string& reason) {
    if (effectRedo_.empty()) {
        reason = "effect-redo-empty";
        return false;
    }
    effectUndo_.push_back(effects_);
    effects_ = std::move(effectRedo_.back());
    effectRedo_.pop_back();
    PruneEffects();
    ++effectRevision_;
    revision_ = nextRevision_++;
    reason.clear();
    return true;
}
}
