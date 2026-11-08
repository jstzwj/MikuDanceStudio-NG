#pragma once
#include "mme_runtime.hpp"
#include "rhi_frame_passes.hpp"
#include "mme_portable_api.h"
#include <functional>
#include <tuple>

namespace mikudancestudio::mme {
// IDs identify host assignments, revisions identify immutable effect reloads.
struct EffectBinding {
    std::uint64_t id = 0, revision = 0;
    std::filesystem::path path;
    std::shared_ptr<EffectInstance> instance;
};
struct TargetDeclaration {
    std::string name, semantic, type;
    bool shared = false, publisher = false;
    MmePortableTarget target;
    std::vector<std::pair<std::string, std::string>> defaults;
    std::array<float, 4> clearColor{};
    float clearDepth = 1;
};
struct TechniqueRule {
    unsigned index = 0;
    int mode = 0;
    std::array<int, 3> use{-1, -1, -1};
    bool allSubsets = true;
    std::map<int, int> subsets;
    std::vector<MmePortableCommand> script;
};
struct ScheduleDescription {
    EffectBinding binding;
    bool scene = false;
    int order = 0; // standard=0, preprocess=1, postprocess=2
    std::vector<TargetDeclaration> targets;
    std::vector<TechniqueRule> techniques;
};
bool DescribeSchedule(const EffectBinding&, ScheduleDescription&, std::string&);
// Validity is supplied by the later compiler/capability stage; unvalidated
// descriptions cannot silently override an explicitly failed technique.
bool SelectTechnique(const ScheduleDescription&, int mode, int subset, unsigned flags,
                     const std::vector<bool>& valid, int& selected, std::string&);

struct TargetLease {
    std::uint64_t id = 0, generation = 0, epoch = 0;
};
namespace detail {
struct TargetRegistryState;
struct ScheduleState;
}
class TargetRegistry {
public:
    TargetRegistry();
    // Reconcile a complete live inventory atomically. Viewport/device changes
    // rebuild identities. Reader-only shared declarations require a publisher.
    bool Reconcile(const std::vector<ScheduleDescription>&, unsigned width, unsigned height,
                   std::uint64_t deviceRevision, std::string&);
    bool Resolve(std::uint64_t effect, const std::string&, TargetLease&, rhi::FrameTarget&,
                 std::string&) const;
    bool Validate(TargetLease, std::string&) const;

private:
    std::shared_ptr<detail::TargetRegistryState> state_;
    friend class ScheduledFrame;
};
struct ScheduledObject {
    std::uint64_t id = 0;
    std::filesystem::path path;
    int order = 0;
    bool accessory = false, visible = true;
    // Per-subset base/sphere/toon presence, in bits 0..2.
    std::vector<unsigned> flags;
    bool sceneCarrier = false;
};
struct Assignment {
    enum class Kind {
        Automatic,
        Effect,
        Host,
        Hidden,
        MainDefault
    } kind = Kind::Automatic;
    std::uint64_t effect = 0;
};
struct AssignmentKey {
    std::uint64_t scope = 0, object = 0;
    int subset = -1;
    bool operator<(const AssignmentKey& other) const {
        return std::tie(scope, object, subset) < std::tie(other.scope, other.object, other.subset);
    }
};
struct AssignmentTable {
    std::map<AssignmentKey, Assignment> effects;
    std::map<AssignmentKey, bool> shown;
    Assignment fallback{Assignment::Kind::Host};
};
// Stable host scope, independent of allocation IDs and viewport rebuilding.
std::uint64_t TargetScope(std::uint64_t effect, unsigned target);
// First explicit subset, then explicit whole object, then ordered DefaultEffect,
// then same-name .fx. Absence and explicit Host/Hidden are distinct.
bool ResolveAssignment(const ScheduledObject&, int subset, std::uint64_t scope, std::uint64_t owner,
                       const std::vector<std::pair<std::string, std::string>>&,
                       const AssignmentTable&, const std::vector<ScheduleDescription>&, Assignment&,
                       std::string&);
struct EmmObject {
    std::string key;
    bool accessory = false;
    std::filesystem::path path;
};
struct EmmSection {
    std::string name, owner;
    std::map<std::string, std::string> effects;
    std::map<std::string, bool> shown;
};
struct EmmMapping {
    std::vector<EmmObject> objects;
    std::vector<EmmSection> sections;
};
// Text is already decoded by the host (legacy CP932 or UTF-8 without BOM).
// No PMM extension block, process cwd mutation, or filesystem probing here.
bool ParseEmm(const std::string&, EmmMapping&, std::string&);
bool ApplyEmm(const EmmMapping&, const std::filesystem::path& directory,
              const std::vector<ScheduledObject>&, const std::vector<ScheduleDescription>&,
              AssignmentTable&, std::string&);

struct PlannedStep {
    enum class Kind {
        Clear,
        Material,
        Buffer,
        Host,
        GenerateMips
    } kind = Kind::Clear;
    std::uint64_t scope = 0, object = 0, effect = 0;
    int subset = -1;
    unsigned technique = 0, pass = 0;
    rhi::FramePass resources;
    ParameterSnapshot parameters;
};
struct ScheduleInput {
    std::vector<ScheduleDescription> effects;
    std::vector<ScheduledObject> objects;
    AssignmentTable assignments;
    // A frozen host context per assignment. Called on CPU only.
    std::function<SemanticContext(std::uint64_t object, int subset, std::uint64_t offscreenOwner)>
        context;
    int mode = 0;
    std::map<std::uint64_t, std::vector<bool>> techniqueValidity;
    bool hostSceneDraws = false;
    rhi::ClearColor clearColor{};
    // Host default depth surface. Explicit FX formats retain their own format.
    rhi::TextureFormat hostDepthFormat = rhi::TextureFormat::Depth24Stencil8;
};
class ScheduledFrame {
public:
    static bool Prepare(const ScheduleInput&, TargetRegistry&, EffectPool&, FrameClock&,
                        std::uint64_t frame, std::uint32_t tick, float rawSeconds, bool edit,
                        std::unique_ptr<ScheduledFrame>&, std::string&);
    ~ScheduledFrame();
    const std::vector<PlannedStep>& Steps() const;
    // Dependency graph only: P5.4 adds shaders/geometry, then runs full RHI
    // validation before submitting. Graph already uses the same pass contract.
    const rhi::RenderFrame& Dependencies() const;
    bool Ready(std::string&) const;
    bool Commit(std::string&);
    std::string Report() const;

private:
    ScheduledFrame();
    std::unique_ptr<detail::ScheduleState> state_;
};
}
