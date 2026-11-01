#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/cpu_scene_simulation.hpp"
#include "mikudancestudio/accessory_resource.hpp"
#include <map>
namespace mikudancestudio::runtime {
struct SceneCounters {
    std::uint64_t frames = 0, preparedModels = 0, publishedModels = 0;
    std::uint64_t editorUpdates = 0;
};
class SceneRuntime final {
public:
    SceneRuntime();
    ~SceneRuntime();
    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;
    bool Load(format::SceneSession, const std::map<std::uint8_t, format::ModelDocument>&,
              std::string&, bool useSavedPose = false);
    bool LoadResources(format::SceneSession,
                       const std::map<std::uint8_t, std::shared_ptr<const ModelResource>>&,
                       std::string&, bool useSavedPose = false);
    bool LoadModel(const format::ModelDocument&, std::string&);
    bool BindMotion(std::uint8_t, std::shared_ptr<const MotionClip>, std::string&);
    bool BindGlobalMotion(const format::VmdDocument&, std::string&);
    bool Seek(std::uint32_t, std::string&, format::PmmSceneApplication = {});
    bool SetPhysicsMode(unsigned mode, std::string& reason);
    void SetPhysicsFloor(bool enabled);
    bool SetModelEdgeScale(std::uint8_t slot, float value);
    bool Reset(std::string&);
    bool StartPlayback(std::string&, format::PmmSceneApplication = {});
    void Pause();
    bool Playing() const;
    bool Advance(float absoluteSeconds, float deltaSeconds, std::string&,
                 format::PmmSceneApplication = {});
    bool Tick(float deltaSeconds, std::string&);
    // Native edit commands latch a reseat for the next simulation frame.
    // This does not rebuild the world or step it during command dispatch.
    void RequestPhysicsReset();
    bool Edit(const std::function<void(format::PmmSceneDocument&)>&, std::string&);
    // Same resources, binding layout, cursor and physics mode: stage an editor
    // document and fresh CPU snapshots without stepping/recreating the world.
    // Unsupported structural changes use LoadResources; rejection is atomic.
    bool CanApplyEditorDocument(const format::PmmSceneDocument&) const;
    bool ApplyEditorDocument(format::SceneSession, std::string&);
    void SetPanelExpanded(unsigned option, bool expanded);
    void SetSidebarWidth(int width);
    void SetGroupExpanded(std::uint8_t slot, unsigned group, bool expanded);
    void ClearPoseEditMarkers();
    bool SelectPhysicsOnKeys(std::uint8_t, const std::vector<std::uint8_t>& physical);
    bool SetSelectedKeys(std::uint8_t, const std::set<format::PmmKeyAddress>&,
                         const format::PmmKeyPools& clearUnused);
    bool SelectKeyRange(std::uint8_t, const format::PmmKeyRangeSelection&);
    void SetBoneSelection(std::uint8_t, const std::vector<std::uint32_t>&, int active);
    void SetEditMode(std::int32_t);
    bool SetViewCamera(const format::PmmEvaluatedCamera&, const std::array<float, 2>&);
    bool SetPicturePlacement(std::int32_t x, std::int32_t y, float scale);
    bool PrepareEditorView(std::uint8_t model, bool& changed);
    bool SetViewBasis(const std::array<float, 16>&, bool suppressed);
    bool SetCameraReference(unsigned mode);
    bool RefreshEditorView(const format::PmmViewRefresh&, std::string&);
    bool SetFollowCamera(bool enabled, const format::PmmEvaluatedCamera&,
                         const std::array<float, 2>&, const cpu_math::Matrix& basis,
                         bool suppressed, std::string&);
    bool UnloadModel(std::uint8_t, std::string&);
    bool AttachAccessory(std::uint8_t, std::shared_ptr<const AccessoryResource>, std::string&);
    bool UnloadAccessory(std::uint8_t, std::string&);
    std::shared_ptr<const AccessoryResource> Accessory(std::uint8_t) const;
    const ModelSnapshot* Snapshot(std::uint8_t) const;
    std::shared_ptr<const ModelResource> Resource(std::uint8_t) const;
    std::vector<std::array<float, 38>> Bodies(std::uint8_t) const;
    const format::SceneSession* Session() const;
    const CpuSimulationState& Simulation() const;
    const SceneCounters& Counters() const;
    // PMM bone indices address its name-bound order, which may differ from the
    // immutable resource. BoneAttachment itself takes a resource bone index.
    bool DocumentBoneIndex(int model, int documentBone, int& resourceBone, std::string&) const;
    bool DocumentMorphIndex(int model, int documentMorph, int& resourceMorph, std::string&) const;
    // Display groups and categories use resource order. Translate them back to
    // document channels before editing, selecting or registering keys.
    bool ResourceBoneIndex(int model, int resourceBone, int& documentBone, std::string&) const;
    bool ResourceMorphIndex(int model, int resourceMorph, int& documentMorph, std::string&) const;
    // Read the last completed CPU frame in PMM bone order. Mode zero returns
    // the editable pose; other modes select the same physics backup as MMD's
    // register command. This never advances simulation or changes the model.
    bool BoneRegistrationPose(std::uint8_t, std::uint32_t documentBone, unsigned physicsMode,
                              format::PmmEvaluatedBone&, bool& physicsControlled,
                              std::string&) const;
    bool BoneAttachment(int model, int resourceBone, cpu_math::Matrix&, std::string&) const;
    bool AccessoryPlacement(std::uint8_t, cpu_math::Matrix&, std::string&) const;

private:
    bool ChannelIndex(int model, int index, bool morph, bool inverse, int&, std::string&) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
