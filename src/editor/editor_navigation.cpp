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
