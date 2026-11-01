#pragma once
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/pmm_scene_world.hpp"
#include "mikudancestudio/pmm_key_selection.hpp"
#include <functional>
#include <cmath>
#include <optional>
#include <string>

namespace mikudancestudio::format {
using ResolveSceneModel = bool (*)(void* context, std::uint8_t slot,
                                   const std::array<char, 256>& path, ModelDocument& model);

// A view refresh applies evaluated globals without seeking models, rebuilding
// physics or consuming playback cursors. The host supplies the reanchored camera.
struct PmmViewRefresh {
    PmmEvaluatedCamera camera;
    std::array<float, 2> offsets{};
    std::array<float, 16> basis{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool suppressed = false, reloadGlobals = false, defaultLight = false;
    std::optional<std::uint8_t> editorModel;
};

// Owns a PMM scene, optional parsed model resources, and its evaluated pose.
// V1 requires a host model resolver for PMD layout; v2 can omit it. Paths
// remain byte strings and are never resolved by this library.
class SceneSession {
public:
    static bool Load(ReadPmmBytesResult read, void* readContext, ResolveSceneModel resolveModel,
                     void* modelContext, SceneSession& output);
    // Editor transaction: materialize a fresh v2 envelope, including changes to
    // linked tracks. Rejection leaves output unchanged. Uses saved current pose
    // rather than seeking (unregistered edits must remain visible).
    static bool Create(const PmmSceneDocument&, SceneSession& output, std::string& reason);
    bool Seek(std::uint32_t frame, PmmSceneApplication = {});
    bool StartPlayback(PmmSceneApplication = {});
    // Absolute playback cursor in seconds, not a per-frame delta.
    bool Advance(float seconds, PmmSceneApplication = {});
    // Optional host application gate: evaluating another subsystem does not
    // necessarily apply its camera/light tracks. Keep displayed state separate
    // from Pose while legacy playback gates remain in use. Standalone hosts
    // omit these overrides and consume Pose directly.
    bool SetRuntimeAppliedLight(const std::array<float, 6>&);
    const std::optional<std::array<float, 6>>& RuntimeAppliedLight() const {
        return runtimeAppliedLight_;
    }
    bool SetRuntimeAppliedCamera(const PmmEvaluatedCamera&);
    // Current view only: no track evaluation or physics reset. Playback cursors
    // continue to own subsequent animated camera values.
    bool SetViewCamera(const PmmEvaluatedCamera&, const std::array<float, 2>& offsets);
    bool SetPicturePlacement(std::int32_t x, std::int32_t y, float scale);
    bool PrepareEditorView(std::uint8_t model, bool playing, bool& changed);
    bool SetViewBasis(const std::array<float, 16>&, bool suppressed);
    // Follow-on reloads global/accessory tracks at the current frame. The host
    // supplies the camera after native model-view reanchoring. Bone poses,
    // playback cursors and physics are retained; rejection changes no state.
    bool SetFollowCamera(bool enabled, const PmmEvaluatedCamera&,
                         const std::array<float, 2>& offsets, const std::array<float, 16>& basis,
                         bool suppressed);
    bool RefreshEditorView(const PmmViewRefresh&);
    bool SetCameraReference(unsigned mode) {
        if (!loaded_ || mode > 2)
            return false;
        scene_.timeline.cameraReferenceMode = std::uint8_t(mode);
        return true;
    }
    const std::optional<PmmEvaluatedCamera>& RuntimeAppliedCamera() const {
        return runtimeAppliedCamera_;
    }
    // Windows runtime bridge: replace one model's linked key tracks after an
    // editor registration. The original byte envelope is then stale, so Save
    // requires reloading a newly serialized envelope first.
    bool ReplaceRuntimeModelTracks(std::uint8_t slot, PmmKeyTrack<PmmBoneKey> bones,
                                   PmmKeyTrack<PmmMorphKey> morphs,
                                   PmmKeyTrack<PmmDisplayKey> displays);
    bool ReplaceRuntimeCameraTrack(PmmKeyTrack<PmmCameraKey> camera);
    bool ReplaceRuntimeLightTrack(PmmKeyTrack<PmmLightKey> light);
    bool ReplaceRuntimeAccessoryTrack(std::uint8_t slot, PmmKeyTrack<PmmAccessoryKey> accessory);
    bool ReplaceRuntimeSelfShadowTrack(PmmKeyTrack<PmmSelfShadowKey> shadow);
    bool ReplaceRuntimeGravityTrack(PmmKeyTrack<PmmGravityKey> gravity);
    // Validates edits by writing and reparsing a private copy before commit.
    // Existing key values and current poses can be edited; track topology is
    // fixed by the PMM writer.
    bool Edit(const std::function<void(PmmSceneDocument&)>& change);
    // The runtime also updates its ground body, without seeking the scene.
    void SetPhysicsFloor(bool enabled) { scene_.appearance.floorVisible = enabled; }
    // Live model property: retain evaluated poses, playback and physics state.
    bool SetModelEdgeScale(std::uint8_t slot, float value) {
        if (!loaded_ || !std::isfinite(value))
            return false;
        for (auto& model : scene_.models)
            if (model.slot == slot) {
                model.edgeScale = value;
                return true;
            }
        return false;
    }
    // UI-only PMM metadata; does not seek or invalidate the evaluated pose.
    void SetPanelExpanded(unsigned option, bool expanded) {
        if (option > 0 && option < scene_.envelope.settings.options.size())
            scene_.envelope.settings.options[option] = expanded;
    }
    void SetSidebarWidth(int width) {
        if (width >= 250 && width <= 1000000)
            scene_.envelope.settings.editPanelWidth = width;
    }
    void SetGroupExpanded(std::uint8_t slot, unsigned group, bool expanded) {
        for (auto& model : scene_.models)
            if (model.slot == slot && group < model.selection.groupFlags.size())
                model.selection.groupFlags[group] = expanded;
    }
    void ClearPoseEditMarkers() {
        for (auto& model : scene_.models)
            for (auto& pose : model.bonePose)
                pose.flags[1] = 0;
    }
    bool SelectPhysicsOnKeys(std::uint8_t slot, const std::vector<std::uint8_t>& physical);
    bool SetSelectedKeys(std::uint8_t slot, const std::set<PmmKeyAddress>& selected,
                         const PmmKeyPools& clearUnused) {
        return loaded_ && SetPmmSelectedKeys(scene_, slot, selected, clearUnused);
    }
    bool SelectKeyRange(std::uint8_t slot, const PmmKeyRangeSelection& range) {
        return loaded_ && SelectPmmKeyRange(scene_, slot, range);
    }
    void SetEditMode(std::int32_t mode) { scene_.timeline.editMode = mode; }
    void SetBoneSelection(std::uint8_t slot, const std::vector<std::uint32_t>& indices,
                          int active) {
        for (auto& model : scene_.models)
            if (model.slot == slot) {
                for (auto& bone : model.bonePose)
                    bone.flags[2] = 0;
                for (auto index : indices)
                    if (index < model.bonePose.size())
                        model.bonePose[index].flags[2] = 1;
                model.selection.selectedBone = active;
            }
    }
    bool Save(WritePmmBytes write, void* context) const;
    bool KinematicWorld(std::uint8_t slot,
                        std::vector<std::optional<std::array<float, 16>>>& world) const;
    bool CameraAttachmentPoint(std::array<float, 3>& point) const;
    bool AccessoryWorld(std::uint8_t slot, PmmWorldMatrix& world) const;

    const PmmSceneDocument& Document() const { return scene_; }
    const PmmSceneEvaluation& Pose() const { return pose_; }
    const PmmScenePlaybackState& Playback() const { return playback_; }
    const std::vector<std::optional<ModelDocument>>& Models() const { return models_; }
    bool Loaded() const { return loaded_; }
    bool Playing() const { return playing_; }
    bool RuntimeTracksChanged() const { return runtimeTracksChanged_; }

private:
    PmmSceneDocument scene_;
    std::vector<std::optional<ModelDocument>> models_;
    PmmSceneEvaluation pose_;
    PmmScenePlaybackState playback_;
    std::optional<std::array<float, 6>> runtimeAppliedLight_;
    std::optional<PmmEvaluatedCamera> runtimeAppliedCamera_;
    bool loaded_ = false, playing_ = false;
    bool runtimeTracksChanged_ = false;
};
}
