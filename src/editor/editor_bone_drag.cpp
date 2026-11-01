#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/bone_drag.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>

namespace mikudancestudio::editor {
namespace {
struct DragBone : cpu_model::Bone {
    float localAxes[6]{};
};
struct DragModel {
    DragBone* boneTable;
    unsigned boneCount;
    int selectedBone, physicsMode;
    unsigned char *boneSelection, *bonePhysicsState;
};
struct DragHost {
    struct {
        int mouseX, mouseY, previousMouseX, previousMouseY, dragOriginX, dragOriginY;
        int coordinateSystem, shiftModifierState, ctrlModifierState;
        float selectedClipW;
    } state;
    struct View {
        int screenWidth, screenHeight;
        float viewScale;
    } view;
    format::PmmEvaluatedCamera camera;
    int renderWidth, renderHeight;
    View* Renderer() { return &view; }
    int MouseX() const { return state.mouseX; }
    int MouseY() const { return state.mouseY; }
    int PreviousMouseX() const { return state.previousMouseX; }
    int PreviousMouseY() const { return state.previousMouseY; }
    bool ShiftModifierActive() const { return state.shiftModifierState == 3; }
    bool CtrlModifierActive() const { return state.ctrlModifierState == 3; }
    int RenderWidth() const { return renderWidth; }
    int RenderHeight() const { return renderHeight; }
    float CameraDistance() const { return camera.distance; }
    auto CameraPerspective() const { return camera.perspective; }
    const float* CameraRotation() const { return camera.rotation.data(); }
};
struct DragMath {
    using Scalar = float;
    inline static constexpr Scalar Wrap = 6.28f;
    static float SinTimes(float angle, float factor) { return std::sin(angle) * factor; }
    static float Cos(float angle) { return std::cos(angle); }
    static void Normalize(float* v) { cpu_model::Math{}.normalize(v, v); }
    static void Multiply(float* out, const float* a, const float* b) {
        cpu_model::Math{}.quatMultiply(out, a, b);
    }
};
}
bool EditorSession::BeginBoneDrag(unsigned mode, std::string& reason) {
    const auto& models = Document().models;
    const auto model = std::find_if(models.begin(), models.end(),
                                    [&](const auto& m) { return m.slot == selection_.model; });
    if (mode < 1 || mode > 12 || ActiveBone() < 0 || model == models.end() ||
        std::any_of(selection_.bones.begin(), selection_.bones.end(),
                    [&](auto i) { return i >= model->bonePose.size(); }) ||
        !BeginGesture()) {
        reason = "bone-drag-unavailable";
        return false;
    }
    // The native host snapshots selected poses on the press edge, even without movement.
    if (!selection_.bones.empty())
        PushHistory(selection_.model,
                    CaptureHistory(*model, HistoryKind::Pose,
                                   std::uint32_t(Document().timeline.currentFrame),
                                   selection_.bones));
    boneDragMode_ = mode;
    if (mode <= 3 || (mode >= 7 && mode <= 9))
        runtime_->SetEditMode(5);
    reason.clear();
    return true;
}
void EditorSession::EndBoneDrag() {
    if (boneDragMode_)
        EndGesture();
}
bool EditorSession::DragBones(const BoneDragInput& input, std::string& reason) {
    // Validate before touching live state, including int subtraction in the native kernel.
    const auto safe = [](int n) { return n >= -1000000 && n <= 1000000; };
    if (playing_ || input.mode < 1 || input.mode > 12 || input.viewWidth <= 0 ||
        input.viewHeight <= 0 || !std::isfinite(input.clipW) || !std::isfinite(input.viewScale) ||
        (input.mode >= 7 && input.mode <= 9 && input.clipW <= 0) || input.viewScale <= 0 ||
        !safe(input.x) || !safe(input.y) || !safe(input.previousX) || !safe(input.previousY) ||
        !safe(input.originX) || !safe(input.originY)) {
        reason = "bone-drag-input-invalid";
        return false;
    }
    auto next = Current();
    auto model = std::find_if(next.document.models.begin(), next.document.models.end(),
                              [&](const auto& m) { return m.slot == selection_.model; });
    const auto resource = runtime_->Resource(selection_.model);
    const auto snapshot = runtime_->Snapshot(selection_.model);
    if (!resource || !snapshot || model == next.document.models.end() || ActiveBone() < 0) {
        reason = "bone-drag-model-unavailable";
        return false;
    }
    const auto count = resource->EditingBones().size();
    std::vector<DragBone> bones(count);
    std::vector<unsigned char> selected(count), dirty(count);
    std::vector<int> indices(model->bonePose.size());
    int active = -1;
    for (std::size_t i = 0; i < count; ++i) {
        auto& bone = bones[i];
        const auto& fields = resource->Document().bones[i].fields;
        const auto& edit = resource->EditingBones()[i];
        const auto& published = snapshot->bones[i];
        std::copy_n(fields.position, 3, bone.position);
        std::copy_n(fields.axis, 3, bone.axis);
        std::copy_n(fields.localAxes, 6, bone.localAxes);
        std::copy(published.skin.begin(), published.skin.end(), bone.matInit);
        std::copy(published.world.begin(), published.world.end(), bone.matWorld);
        std::copy(published.translation.begin(), published.translation.end(), bone.trans);
        std::copy(published.rotation.begin(), published.rotation.end(), bone.rotQuat);
        bone.parent = fields.parent;
        // Native PMX offset tails retain the zero-initialized tail index.
        bone.tailBone = edit.tailBone < 0 ? 0 : edit.tailBone;
        bone.type = edit.type;
        bone.flags = edit.flags;
        std::string name;
        if (text::EncodeCp932(resource->BoneNames()[i], name))
            std::copy_n(name.data(), std::min(name.size(), sizeof bone.name - 1), bone.name);
    }
    for (std::size_t i = 0; i < indices.size(); ++i) {
        int index;
        if (!runtime_->DocumentBoneIndex(selection_.model, int(i), index, reason))
            return false;
        indices[i] = index;
        auto& bone = bones[std::size_t(index)];
        std::copy_n(model->bonePose[i].components.begin(), 3, bone.trans);
        std::copy_n(model->bonePose[i].components.begin() + 3, 4, bone.rotQuat);
        if (int(i) == ActiveBone())
            active = index;
    }
    for (auto index : selection_.bones) {
        if (index >= indices.size()) {
            reason = "bone-selection-expired";
            return false;
        }
        selected[std::size_t(indices[index])] = 1;
    }
    if (active < 0) {
        reason = "bone-selection-expired";
        return false;
    }
    if (input.x == input.previousX && input.y == input.previousY) {
        reason.clear();
        return true;
    }
    DragHost host{{input.x, input.y, input.previousX, input.previousY, input.originX, input.originY,
                   int(coordinateSystem_), input.shift ? 3 : 0, input.control ? 3 : 0, input.clipW},
                  {input.viewWidth, input.viewHeight, input.viewScale},
                  runtime_->Session()->Pose().camera,
                  int(next.document.envelope.settings.renderWidth),
                  int(next.document.envelope.settings.renderHeight)};
    DragModel view{bones.data(),    unsigned(count),
                   active,          resource->SourceKind() == format::ModelKind::Pmx ? 2 : 1,
                   selected.data(), dirty.data()};
    cpu_bone::DragKernel<DragHost, DragModel, DragBone, DragMath>::Run(&host, &view, input.mode);
    std::vector<std::uint32_t> changed;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto index = std::size_t(indices[i]);
        if (!dirty[index])
            continue;
        const auto& bone = bones[index];
        for (float value : bone.trans)
            if (!std::isfinite(value)) {
                reason = "bone-drag-nonfinite";
                return false;
            }
        for (float value : bone.rotQuat)
            if (!std::isfinite(value)) {
                reason = "bone-drag-nonfinite";
                return false;
            }
        changed.push_back(std::uint32_t(i));
    }
    if (changed.empty()) {
        reason.clear();
        return true;
    }
    const auto history = CaptureHistory(
        *model, HistoryKind::Pose, std::uint32_t(next.document.timeline.currentFrame), changed);
    for (auto index : changed) {
        const auto& bone = bones[std::size_t(indices[index])];
        auto& pose = model->bonePose[index];
        std::copy_n(bone.trans, 3, pose.components.begin());
        std::copy_n(bone.rotQuat, 4, pose.components.begin() + 3);
        pose.flags[1] = 1;
    }
    return Commit(std::move(next), reason, {{selection_.model, history}});
}
}
