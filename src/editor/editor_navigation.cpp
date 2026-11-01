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
        using namespace cpu_math;
        auto basis = Document().appearance.cameraAttachmentBasis;
        auto rotation =
            Multiply(Multiply(RotationY(camera.rotation[1]), RotationX(camera.rotation[0])),
                     RotationZ(camera.rotation[2]));
        Vec3 reference{};
        const auto resource = runtime_->Resource(selection_.model);
        const auto snapshot = runtime_->Snapshot(selection_.model);
        if (!resource || !snapshot) {
            reason = "editor-view-reference-expired";
            return false;
        }
        if (Document().timeline.cameraReferenceMode == 1) {
            const auto& skin = snapshot->bones[resource->CameraCenterBone()].skin;
            std::copy_n(skin.begin() + 12, 3, reference.begin());
        } else if (Document().timeline.cameraReferenceMode == 2) {
            const int bone = camera.parentModel >= 0 ? std::max(0, ActiveBone()) : ActiveBone();
            if (bone >= 0)
                reference = snapshot->bones[std::size_t(bone)].worldPosition;
        }
        if (camera.parentModel >= 0) {
            Matrix parent;
            if (!runtime_->BoneAttachment(camera.parentModel, std::max(0, camera.parentBone),
                                          parent, reason))
                return false;
            basis = Identity();
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 3; ++c)
                    basis[4 * r + c] = parent[4 * c + r];
            rotation = Multiply(basis, rotation);
            const auto target =
                Multiply(Translation({parent[12], parent[13], parent[14]}), rotation);
            const auto pivot = Multiply(Translation(reference), rotation);
            offsets = {target[12] - pivot[12] + camera.position[0],
                       target[13] - pivot[13] + camera.position[1]};
            camera.distance = target[14] - pivot[14] + camera.position[2];
        } else {
            const auto target = Multiply(
                Translation({-camera.position[0], -camera.position[1], -camera.position[2]}),
                rotation);
            const auto pivot =
                Multiply(Translation({-reference[0], -reference[1], -reference[2]}), rotation);
            offsets = {-(target[12] - pivot[12]), -(target[13] - pivot[13])};
            camera.distance -= target[14] - pivot[14];
        }
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
