#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/camera_navigation.hpp"
#include <cmath>

namespace mikudancestudio::editor {
namespace {
bool Same(const format::PmmEvaluatedCamera& a, const format::PmmEvaluatedCamera& b) {
    return a.position == b.position && a.rotation == b.rotation && a.distance == b.distance &&
           a.fov == b.fov && a.perspective == b.perspective && a.parentModel == b.parentModel &&
           a.parentBone == b.parentBone;
}
bool FollowedModel(const EditorSession& session) {
    const auto parent = session.Runtime().Session()->Pose().camera.parentModel;
    return session.Selected().model != 255 && parent >= 0 && session.Selected().model == parent &&
           !session.Playing() && session.Document().appearance.followCamera;
}
std::array<float, 2> Offsets(const EditorSession& session) {
    return {session.Document().cameraState[3], session.Document().cameraState[4]};
}
}
bool EditorSession::ApplyView(const format::PmmEvaluatedCamera& camera,
                              const std::array<float, 2>& offsets, std::string& reason) {
    const bool changed =
        !Same(camera, runtime_->Session()->Pose().camera) || offsets != Offsets(*this);
    if (!runtime_->SetViewCamera(camera, offsets)) {
        reason = "invalid-view-camera";
        return false;
    }
    if (changed)
        revision_ = nextRevision_++;
    reason.clear();
    return true;
}
bool EditorSession::SetCamera(const format::PmmEvaluatedCamera& camera, std::string& reason) {
    if (!std::isfinite(camera.fov) || camera.fov < 1 || camera.fov > 179) {
        reason = "camera-fov-out-of-range";
        return false;
    }
    return ApplyView(camera, Offsets(*this), reason);
}
bool EditorSession::AppliesGlobalTracks() const {
    return selection_.model == 255 || Document().appearance.followCamera;
}
bool EditorSession::RefreshTrackedView(bool reanchor, std::string& reason) {
    auto camera = runtime_->Session()->Pose().camera;
    std::array<float, 2> offsets{};
    auto basis = Document().appearance.cameraAttachmentBasis;
    if (reanchor && !ReanchorModelView(camera, offsets, basis, reason))
        return false;
    if (!ApplyView(camera, offsets, reason))
        return false;
    if (reanchor && !runtime_->SetViewBasis(basis, false)) {
        reason = "editor-view-state-unavailable";
        return false;
    }
    return true;
}
bool EditorSession::PrepareView(std::string& reason) {
    bool changed = false;
    if (!runtime_->PrepareEditorView(selection_.model, changed)) {
        reason = "editor-view-state-unavailable";
        return false;
    }
    if (changed)
        revision_ = nextRevision_++;
    reason.clear();
    return true;
}
bool EditorSession::ViewReferencePoint(unsigned mode, bool fallbackToRoot, cpu_math::Vec3& out,
                                       std::string& reason) const {
    out = {};
    if (mode != 1 && mode != 2)
        return true;
    const auto resource = runtime_->Resource(selection_.model);
    const auto snapshot = runtime_->Snapshot(selection_.model);
    if (!resource || !snapshot) {
        reason = "editor-view-reference-expired";
        return false;
    }
    if (mode == 1) {
        const auto& skin = snapshot->bones[resource->CameraCenterBone()].skin;
        std::copy_n(skin.begin() + 12, 3, out.begin());
    } else {
        int bone = ActiveBone();
        if (fallbackToRoot)
            bone = std::max(0, bone);
        if (bone >= 0) {
            int index;
            if (!runtime_->DocumentBoneIndex(selection_.model, bone, index, reason))
                return false;
            out = snapshot->bones[std::size_t(index)].worldPosition;
        }
    }
    return true;
}
bool EditorSession::SetCameraReference(unsigned mode, std::string& reason) {
    if (mode > 2) {
        reason = "invalid-camera-reference-mode";
        return false;
    }
    const auto previous = Document().timeline.cameraReferenceMode;
    if (previous == mode) {
        reason.clear();
        return true;
    }
    if (selection_.model != 255) {
        using namespace cpu_math;
        auto camera = runtime_->Session()->Pose().camera;
        auto rotation =
            Multiply(Multiply(RotationY(camera.rotation[1]), RotationX(camera.rotation[0])),
                     RotationZ(camera.rotation[2]));
        // The command uses the stored basis whenever there is a parent,
        // independently of the frame's follow/suppression gates.
        if (camera.parentModel >= 0)
            rotation = Multiply(Document().appearance.cameraAttachmentBasis, rotation);
        Vec3 oldPoint, newPoint;
        if (!ViewReferencePoint(previous, true, oldPoint, reason) ||
            !ViewReferencePoint(mode, true, newPoint, reason))
            return false;
        const auto oldOrigin =
            Multiply(Translation({-oldPoint[0], -oldPoint[1], -oldPoint[2]}), rotation);
        const auto newOrigin =
            Multiply(Translation({-newPoint[0], -newPoint[1], -newPoint[2]}), rotation);
        auto offsets = Offsets(*this);
        offsets[0] -= oldOrigin[12] - newOrigin[12];
        offsets[1] -= oldOrigin[13] - newOrigin[13];
        camera.distance -= oldOrigin[14] - newOrigin[14];
        if (!ApplyView(camera, offsets, reason))
            return false;
    }
    if (!runtime_->SetCameraReference(mode)) {
        reason = "editor-view-state-unavailable";
        return false;
    }
    revision_ = nextRevision_++;
    reason.clear();
    return true;
}
bool EditorSession::ReanchorModelView(format::PmmEvaluatedCamera& camera,
                                      std::array<float, 2>& offsets, cpu_math::Matrix& basis,
                                      std::string& reason) const {
    using namespace cpu_math;
    auto rotation = Multiply(Multiply(RotationY(camera.rotation[1]), RotationX(camera.rotation[0])),
                             RotationZ(camera.rotation[2]));
    Vec3 reference;
    if (!ViewReferencePoint(Document().timeline.cameraReferenceMode, camera.parentModel >= 0,
                            reference, reason))
        return false;
    if (camera.parentModel >= 0) {
        Matrix parent;
        int index;
        if (!runtime_->DocumentBoneIndex(camera.parentModel, std::max(0, camera.parentBone), index,
                                         reason) ||
            !runtime_->BoneAttachment(camera.parentModel, index, parent, reason))
            return false;
        basis = Identity();
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                basis[4 * r + c] = parent[4 * c + r];
        rotation = Multiply(basis, rotation);
        const auto target = Multiply(Translation({parent[12], parent[13], parent[14]}), rotation);
        const auto pivot = Multiply(Translation(reference), rotation);
        offsets = {target[12] - pivot[12] + camera.position[0],
                   target[13] - pivot[13] + camera.position[1]};
        camera.distance = target[14] - pivot[14] + camera.position[2];
    } else {
        const auto target = Multiply(
            Translation({-camera.position[0], -camera.position[1], -camera.position[2]}), rotation);
        const auto pivot =
            Multiply(Translation({-reference[0], -reference[1], -reference[2]}), rotation);
        offsets[0] -= target[12] - pivot[12];
        offsets[1] -= target[13] - pivot[13];
        camera.distance -= target[14] - pivot[14];
    }
    return true;
}
bool EditorSession::SetFollowCamera(bool enabled, std::string& reason) {
    if (bool(Document().appearance.followCamera) == enabled) {
        reason.clear();
        return true;
    }
    auto camera = runtime_->Session()->Pose().camera;
    auto offsets = Offsets(*this);
    auto basis = Document().appearance.cameraAttachmentBasis;
    bool suppressed = Document().appearance.suppressCameraTransform;
    if (enabled) {
        if (!format::EvaluatePmmCameraTrack(Document().cameraKeys, Document().timeline.currentFrame,
                                            format::PmmCameraInterpolation::Seek, camera)) {
            reason = "camera-track-reload-failed";
            return false;
        }
        offsets = {};
    }
    const bool reanchor =
        selection_.model != 255 &&
        (enabled || (camera.parentModel >= 0 && camera.parentModel == selection_.model));
    if (reanchor) {
        if (!ReanchorModelView(camera, offsets, basis, reason))
            return false;
        suppressed = false;
    }
    if (!runtime_->SetFollowCamera(enabled, camera, offsets, basis, suppressed, reason))
        return false;
    revision_ = nextRevision_++;
    return true;
}
bool EditorSession::ViewPreset(unsigned number, std::string& reason) {
    navigation::ViewPreset preset;
    if (playing_ || !navigation::Preset(number, preset)) {
        reason.clear();
        return true;
    }
    const bool modelMode = selection_.model != 255;
    if (modelMode && FollowedModel(*this)) {
        reason.clear();
        return true;
    }
    auto camera = runtime_->Session()->Pose().camera;
    auto offsets = Offsets(*this);
    if (modelMode && preset.reload) {
        // ReloadModels is the native camera-track reload, not a model reset.
        // It preserves unregistered bone poses and the current timeline frame.
        if (!format::EvaluatePmmCameraTrack(Document().cameraKeys, Document().timeline.currentFrame,
                                            format::PmmCameraInterpolation::Seek, camera)) {
            reason = "camera-track-reload-failed";
            return false;
        }
        offsets = {};
        auto basis = Document().appearance.cameraAttachmentBasis;
        if (!ReanchorModelView(camera, offsets, basis, reason))
            return false;
        if (!ApplyView(camera, offsets, reason) || !runtime_->SetViewBasis(basis, false))
            return false;
        revision_ = nextRevision_++;
        return true;
    }
    camera.rotation = {preset.pitch, preset.yaw, 0};
    camera.distance = modelMode ? preset.distance : -45;
    bool suppressed = false;
    if (modelMode) {
        offsets = {0, preset.referenceHeight && Document().timeline.cameraReferenceMode != 2 ? 10.f
                                                                                             : 0.f};
        suppressed = Document().appearance.suppressCameraTransform ||
                     (camera.parentModel >= 0 && Document().appearance.followCamera);
    }
    if (!ApplyView(camera, offsets, reason))
        return false;
    // Basis-only changes also invalidate the viewport.
    if (modelMode) {
        runtime_->SetViewBasis(cpu_math::Identity(), suppressed);
        revision_ = nextRevision_++;
    } else {
        selection_.tracks.clear();
        selection_.tracks.insert({Track::Camera, 255, 0, 0});
    }
    return true;
}
bool EditorSession::DragView(bool middle, int dx, int dy, bool shift, bool control,
                             std::string& reason) {
    auto camera = runtime_->Session()->Pose().camera;
    auto offsets = Offsets(*this);
    const bool modelMode = selection_.model != 255;
    if (!middle && FollowedModel(*this)) {
        reason.clear();
        return true;
    }
    if (!middle)
        navigation::RightDrag(camera.rotation.data(), camera.distance, offsets.data(), modelMode,
                              dx, dy, shift, control);
    else if (modelMode)
        navigation::PanOffsets(offsets.data(), dx, dy, shift, control);
    else if (camera.parentModel >= 0)
        navigation::PanParentedCamera(camera.position.data(), camera.fov, dx, dy, shift, control);
    else {
        using namespace cpu_math;
        const auto rotation =
            Multiply(RotationZ(-camera.rotation[2]),
                     Multiply(RotationX(camera.rotation[0]), RotationY(camera.rotation[1])));
        navigation::PanFreeCamera(camera.position.data(), rotation.data(), dx, dy, shift, control);
    }
    if (!ApplyView(camera, offsets, reason))
        return false;
    const bool accessoryRow =
        std::any_of(selection_.tracks.begin(), selection_.tracks.end(),
                    [](const auto& row) { return row.track == Track::Accessory; });
    if (!middle && !modelMode && !accessoryRow) {
        selection_.tracks.clear();
        selection_.tracks.insert({Track::Camera, 255, 0, 0});
    }
    return true;
}
bool EditorSession::WheelView(int delta, std::string& reason) {
    if (FollowedModel(*this)) {
        reason.clear();
        return true;
    }
    auto camera = runtime_->Session()->Pose().camera;
    navigation::Wheel(camera.position.data(), camera.distance, delta, selection_.model == 255,
                      camera.parentModel);
    if (!ApplyView(camera, Offsets(*this), reason))
        return false;
    const KeyAddress row{Track::Camera, 255, 0, 0};
    if (!selection_.tracks.count(row)) {
        selection_.tracks.clear();
        selection_.tracks.insert(row);
    }
    return true;
}
}
