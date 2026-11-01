#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/cpu_scene_simulation.hpp"
#include "mikudancestudio/accessory_resource.hpp"
#include <map>
namespace mikudancestudio::runtime {
struct SceneCounters {
    std::uint64_t frames = 0, preparedModels = 0, publishedModels = 0;
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
    bool Reset(std::string&);
    bool StartPlayback(std::string&, format::PmmSceneApplication = {});
    void Pause();
    bool Playing() const;
    bool Advance(float absoluteSeconds, float deltaSeconds, std::string&,
                 format::PmmSceneApplication = {});
    bool Tick(float deltaSeconds, std::string&);
    bool Edit(const std::function<void(format::PmmSceneDocument&)>&, std::string&);
    void SetPanelExpanded(unsigned option, bool expanded);
    void SetGroupExpanded(std::uint8_t slot, unsigned group, bool expanded);
    void ClearPoseEditMarkers();
    void SetBoneSelection(std::uint8_t, const std::vector<std::uint32_t>&, int active);
    void SetEditMode(std::int32_t);
    bool SetViewCamera(const format::PmmEvaluatedCamera&, const std::array<float, 2>&);
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
    bool BoneAttachment(int model, int resourceBone, cpu_math::Matrix&, std::string&) const;
    bool AccessoryPlacement(std::uint8_t, cpu_math::Matrix&, std::string&) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
