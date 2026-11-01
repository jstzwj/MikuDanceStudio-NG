#include "mikudancestudio/editor_session.hpp"
namespace mikudancestudio::editor {
namespace {
bool ModelTrack(Track t) {
    return t == Track::Bone || t == Track::Morph || t == Track::Display;
}
bool Active(const Selection& selected, const KeyAddress& key) {
    return selected.model == 255 ? !ModelTrack(key.track)
                                 : ModelTrack(key.track) && key.slot == selected.model;
}
struct ProjectedBone : runtime::BoneEditingInfo {
    int screenX = 393939, screenY = 393939;
    bool physicsDisabled = false;
};
bool Projected(const EditorSession& session, const std::vector<cpu_bone::ScreenPoint>& points,
               std::vector<ProjectedBone>& result, int& active, std::string& reason) {
    const auto slot = session.Selected().model;
    const auto& models = session.Document().models;
    const auto model = std::find_if(models.begin(), models.end(),
                                    [slot](const auto& m) { return m.slot == slot; });
    const auto resource = session.Runtime().Resource(slot);
    if (session.Playing() || model == models.end() || !resource ||
        points.size() != model->bonePose.size()) {
        reason = "bone-screen-selection-expired";
        return false;
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        int index;
        if (!session.Runtime().DocumentBoneIndex(slot, int(i), index, reason))
            return false;
        ProjectedBone bone;
        static_cast<runtime::BoneEditingInfo&>(bone) = resource->EditingBones()[std::size_t(index)];
        bone.screenX = points[i].x;
        bone.screenY = points[i].y;
        bone.physicsDisabled = model->bonePose[i].flags[0] != 0;
        result.push_back(bone);
    }
    active = model->selection.selectedBone;
    return true;
}
}
bool EditorSession::SelectModel(std::uint8_t slot, std::string& reason) {
    if (playing_) {
        reason = "model-selection-during-playback";
        return false;
    }
    if (slot != 255 && !runtime_->Resource(slot)) {
        reason = "model-selection-expired";
        return false;
    }
    const auto previous = selection_.model;
    if (previous == slot && slot == 255) {
        reason.clear();
        return true;
    }
    const bool cameraMode = slot == 255, wasCamera = previous == 255;
    const bool follow = Document().appearance.followCamera;
    format::PmmViewRefresh refresh;
    refresh.camera = runtime_->Session()->Pose().camera;
    refresh.offsets = {Document().cameraState[3], Document().cameraState[4]};
    refresh.basis = Document().appearance.cameraAttachmentBasis;
    refresh.suppressed = Document().appearance.suppressCameraTransform;
    refresh.editorModel = slot;
    refresh.reloadGlobals = cameraMode || (wasCamera && follow);
    refresh.defaultLight = wasCamera && !follow;
    const bool trackedSwitch =
        !cameraMode && !wasCamera && follow && refresh.camera.parentModel >= 0 &&
        (previous == refresh.camera.parentModel || slot == refresh.camera.parentModel);
    if (refresh.reloadGlobals || trackedSwitch) {
        if (!format::EvaluatePmmCameraTrack(Document().cameraKeys, Document().timeline.currentFrame,
                                            format::PmmCameraInterpolation::Seek, refresh.camera)) {
            reason = "camera-track-reload-failed";
            return false;
        }
        refresh.offsets = {};
    }
    if (cameraMode)
        refresh.basis = cpu_math::Identity();
    else if (wasCamera || trackedSwitch) {
        if (!ReanchorModelView(slot, refresh.camera, refresh.offsets, refresh.basis, reason))
            return false;
        refresh.suppressed = false;
    }
    if (!runtime_->RefreshEditorView(refresh, reason))
        return false;
    EndGesture();
    RestoreModelSelection(slot, reason);
    if (slot != 255 && coordinateSystem_ == 2)
        coordinateSystem_ = 0;
    revision_ = nextRevision_++;
    return true;
}
bool EditorSession::RestoreModelSelection(std::uint8_t slot, std::string& reason) {
    if (slot != 255 && !runtime_->Resource(slot)) {
        reason = "model-selection-expired";
        return false;
    }
    const auto accessory = selection_.accessory;
    selection_ = {};
    if (state_.assets.accessories.count(accessory))
        selection_.accessory = accessory;
    selection_.model = slot;
    for (const auto& model : Document().models)
        if (model.slot == slot)
            for (unsigned i = 0; i < model.bonePose.size(); ++i)
                if (model.bonePose[i].flags[2])
                    selection_.bones.push_back(i);
    reason.clear();
    return true;
}
int EditorSession::ActiveBone() const {
    return ActiveBone(selection_.model);
}
int EditorSession::ActiveBone(std::uint8_t slot) const {
    for (const auto& model : Document().models)
        if (model.slot == slot && model.selection.selectedBone >= 0 &&
            std::size_t(model.selection.selectedBone) < model.bonePose.size())
            return model.selection.selectedBone;
    return -1;
}
bool EditorSession::SelectAllBones(bool unregisteredOnly, std::string& reason) {
    const auto& models = Document().models;
    const auto model = std::find_if(models.begin(), models.end(),
                                    [&](const auto& m) { return m.slot == selection_.model; });
    if (model == models.end()) {
        reason = "bone-selection-expired";
        return false;
    }
    std::vector<cpu_bone::ScreenPoint> points(model->bonePose.size());
    std::vector<ProjectedBone> bones;
    int active = -1;
    if (!Projected(*this, points, bones, active, reason))
        return false;
    std::set<std::uint32_t> selected;
    if (!unregisteredOnly)
        selected.insert(selection_.bones.begin(), selection_.bones.end());
    for (unsigned i = 0; i < bones.size(); ++i)
        if (unregisteredOnly ? model->bonePose[i].flags[1] != 0
                             : bones[i].type <= cpu_bone::BoneType::Effector ||
                                   bones[i].type == cpu_bone::BoneType::FixedAxis)
            selected.insert(i);
    if (!SelectBones({selected.begin(), selected.end()}, unregisteredOnly ? -1 : active, reason))
        return false;
    SetEditMode(0);
    return true;
}
void EditorSession::SetEditMode(std::int32_t mode) {
    if (mode >= 0 && mode <= 4)
        runtime_->SetEditMode(mode);
}
bool EditorSession::SelectPhysicsBones(std::string& reason) {
    const auto slot = selection_.model;
    const auto resource = runtime_->Resource(slot);
    if (playing_ || !resource) {
        reason = "physics-bone-selection-unavailable";
        return false;
    }
    std::vector<std::uint32_t> indices;
    int active = ActiveBone();
    for (unsigned i = 0; i < resource->EditingBones().size(); ++i) {
        int mapped;
        if (!runtime_->DocumentBoneIndex(slot, int(i), mapped, reason))
            return false;
        if (resource->EditingBones()[std::size_t(mapped)].hasRigidBody) {
            indices.push_back(i);
            active = int(i);
        }
    }
    return SelectBones(indices, active, reason);
}
void EditorSession::CycleCoordinateSystem() {
    coordinateSystem_ = (coordinateSystem_ + 1) % (selection_.model == 255 ? 3u : 2u);
}
bool EditorSession::SelectBones(const std::vector<std::uint32_t>& indices, int active,
                                std::string& reason) {
    const auto resource = runtime_->Resource(selection_.model);
    if (!resource || active < -1 || active >= int(resource->BoneNames().size()) ||
        std::any_of(
            indices.begin(), indices.end(),
            [&](auto i) { return i >= resource->BoneNames().size(); })) {
        reason = "bone-selection-expired";
        return false;
    }
    const std::set<std::uint32_t> sorted(indices.begin(), indices.end());
    selection_.bones.assign(sorted.begin(), sorted.end());
    runtime_->SetBoneSelection(selection_.model, selection_.bones, active);
    reason.clear();
    return true;
}
bool EditorSession::PickBone(const std::vector<cpu_bone::ScreenPoint>& points, int x, int y,
                             bool additive, std::string& reason) {
    std::vector<ProjectedBone> bones;
    int active = -1;
    if (!Projected(*this, points, bones, active, reason))
        return false;
    std::vector<std::uint8_t> selected(bones.size(), 0);
    for (auto i : selection_.bones)
        if (i < selected.size())
            selected[i] = 1;
    cpu_bone::Pick(
        unsigned(bones.size()), [&](auto i) -> const auto& { return bones[i]; },
        [&](auto i) -> auto& { return selected[i]; }, active, x, y, Document().physics.playbackMode,
        additive);
    std::vector<std::uint32_t> indices;
    for (unsigned i = 0; i < selected.size(); ++i)
        if (selected[i])
            indices.push_back(i);
    return SelectBones(indices, active, reason);
}
bool EditorSession::BoxSelectBones(const std::vector<cpu_bone::ScreenPoint>& points, int left,
                                   int top, int right, int bottom, bool additive, bool finish,
                                   std::string& reason) {
    std::vector<ProjectedBone> bones;
    int active = -1;
    if (!Projected(*this, points, bones, active, reason))
        return false;
    std::vector<std::uint8_t> selected(bones.size(), 0);
    for (auto i : selection_.bones)
        if (i < selected.size())
            selected[i] = 1;
    const auto selectedAt = [&](auto i) -> auto& { return selected[i]; };
    cpu_bone::Box(
        unsigned(bones.size()), [&](auto i) -> const auto& { return bones[i]; }, selectedAt, left,
        top, right, bottom, additive);
    if (finish)
        cpu_bone::FinishBox(unsigned(bones.size()), selectedAt, active, additive);
    std::vector<std::uint32_t> indices;
    for (unsigned i = 0; i < selected.size(); ++i)
        if (selected[i])
            indices.push_back(i);
    return SelectBones(indices, active, reason);
}
void EditorSession::SelectKeys(std::uint32_t first, std::uint32_t last, const KeyScope& scope) {
    const auto matches = [&](const KeyAddress& key) {
        if (!Active(selection_, key))
            return false;
        switch (scope.kind) {
        case KeyScopeKind::All:
            return true;
        case KeyScopeKind::Bones:
            return key.track == Track::Bone;
        case KeyScopeKind::Morphs:
            return key.track == Track::Morph;
        case KeyScopeKind::SelectedBones:
            return key.track == Track::Bone &&
                   std::find(selection_.bones.begin(), selection_.bones.end(), key.channel) !=
                       selection_.bones.end();
        case KeyScopeKind::SelectedMorphs: {
            auto row = key;
            row.frame = 0;
            return key.track == Track::Morph && selection_.tracks.count(row) != 0;
        }
        case KeyScopeKind::Track:
            return key.track == scope.track.track && key.slot == scope.track.slot &&
                   key.channel == scope.track.channel;
        }
        return false;
    };
    // Native array sweeps replace marks only in their addressed tracks.
    // MarkKeyTrackRangeByName (selected/named model rows) adds marks instead.
    const bool replace = scope.kind == KeyScopeKind::All || scope.kind == KeyScopeKind::Bones ||
                         scope.kind == KeyScopeKind::Morphs ||
                         (scope.kind == KeyScopeKind::Track && !ModelTrack(scope.track.track));
    if (replace)
        for (auto it = selection_.keys.begin(); it != selection_.keys.end();)
            if (matches(*it))
                it = selection_.keys.erase(it);
            else
                ++it;
    for (const auto& key : Keys())
        if (matches(key) && first <= key.frame && key.frame <= last)
            selection_.keys.insert(key);
}
bool EditorSession::SelectPhysicsOnKeys(std::string& reason) {
    reason.clear();
    if (selection_.model == 255)
        return true;
    const auto slot = selection_.model;
    const auto resource = runtime_->Resource(slot);
    const auto& models = Document().models;
    const auto model =
        std::find_if(models.begin(), models.end(), [&](const auto& m) { return m.slot == slot; });
    if (playing_ || !resource || model == models.end()) {
        reason = "physics-key-selection-unavailable";
        return false;
    }
    std::vector<std::uint8_t> physical(model->boneKeys.dense.size());
    for (unsigned bone = 0; bone < physical.size(); ++bone) {
        int index;
        if (!runtime_->DocumentBoneIndex(slot, int(bone), index, reason))
            return false;
        physical[bone] = resource->EditingBones()[std::size_t(index)].hasRigidBody;
    }
    std::map<std::uint32_t, std::uint32_t> channels;
    if (!format::PmmRecordChannels(model->boneKeys, channels) ||
        !runtime_->SelectPhysicsOnKeys(slot, physical)) {
        reason = "physics-key-selection-invalid-tracks";
        return false;
    }
    for (auto it = selection_.keys.begin(); it != selection_.keys.end();)
        if (ModelTrack(it->track) && it->slot == slot)
            it = selection_.keys.erase(it);
        else
            ++it;
    const auto select = [&](unsigned index, const auto& key) {
        if (key.allocated)
            selection_.keys.insert({Track::Bone, slot, channels.at(index), key.frame});
    };
    for (unsigned i = 0; i < model->boneKeys.dense.size(); ++i)
        select(i, model->boneKeys.dense[i]);
    for (const auto& key : model->boneKeys.sparse)
        select(unsigned(key.index), key.value);
    return true;
}
void EditorSession::ExtendSelectedColumns() {
    std::set<std::uint32_t> frames;
    for (const auto& key : selection_.keys)
        if (Active(selection_, key))
            frames.insert(key.frame);
    for (const auto& key : Keys())
        if (Active(selection_, key) && frames.count(key.frame))
            selection_.keys.insert(key);
}
}
