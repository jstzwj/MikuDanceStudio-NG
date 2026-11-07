#pragma once
#include "mme_frontend.hpp"
#include "mme_standard_api.h"

namespace mikudancestudio::mme {
// Logical FX values, before backend constant packing. All numeric components
// are 32-bit; matrices are row-major. No GPU pointers or native handles.
struct ParameterValue {
    enum class Type {
        Float,
        Int,
        Bool,
        String
    } type = Type::Float;
    unsigned rows = 1, columns = 1, elements = 1;
    bool array = false;
    std::vector<std::uint32_t> words;
    std::string text;
    bool operator==(const ParameterValue&) const;
};
// Bounded supported expression subset. Failure preserves output and names the
// offending declaration. References may name earlier static const declarations.
bool EvaluateDefaults(const EffectDefinition&, std::map<std::string, ParameterValue>&,
                      std::string& reason);

namespace detail {
struct PoolState;
struct InstanceState;
struct ClockState;
struct FrameState;
}
class EffectPool {
public:
    EffectPool();

private:
    std::shared_ptr<detail::PoolState> state_;
    friend class EffectInstance;
    friend class EffectFrame;
};
class EffectInstance {
public:
    static bool Create(std::shared_ptr<const EffectDefinition>, EffectPool&,
                       std::shared_ptr<EffectInstance>&, std::string& reason);
    bool Clone(std::shared_ptr<EffectInstance>&, std::string& reason) const;
    bool Read(const std::string&, ParameterValue&, std::string& reason) const;
    // An explicit host-phase write is immediately visible to other instances,
    // as native SetValue is. GPU-preparation writes use EffectFrame instead.
    bool Write(const std::string&, const ParameterValue&, std::string& reason);
    const EffectDefinition& Definition() const;

private:
    EffectInstance() = default;
    EffectInstance(const EffectInstance&) = delete;
    EffectInstance& operator=(const EffectInstance&) = delete;
    std::shared_ptr<detail::InstanceState> state_;
    friend class EffectFrame;
};
struct ControlItem {
    bool matrix = false;
    MmeStandardInputs::Matrix transform{};
    std::array<float, 3> value{};
};
struct ControlObject {
    std::uint64_t id = 0, attachedModel = 0;
    std::string name, attachedBone;
    int order = 0;
    bool accessory = false, visible = true;
    MmeStandardInputs::Matrix world{};
    // Host freezes bone worlds, morph weights and accessory pseudo-items here.
    std::map<std::string, ControlItem> items;
};
struct SemanticContext {
    MmeStandardInputs standard;
    std::vector<ControlObject> objects;
    std::uint64_t owner = 0, offscreenOwner = 0;
};
// Extracted control-value conversion, shared with the Windows SAS adapter.
// Missing object defaults differ from a present object with a missing item.
struct ControlSample {
    bool found = false, accessory = false, hasItem = false;
    bool visible = false, matrix = false;
    MmeStandardInputs::Matrix transform{};
};
bool EvaluateControl(const MmeLegacyVariable&, const ControlSample&, ParameterValue&,
                     std::string& reason);
bool EvaluateSemantics(const EffectDefinition&, const SemanticContext&,
                       std::map<std::string, ParameterValue>& writes, std::string& reason);

class FrameClock {
public:
    FrameClock();

private:
    std::shared_ptr<detail::ClockState> state_;
    friend class EffectFrame;
};
struct ParameterSnapshot {
    std::shared_ptr<const EffectDefinition> definition;
    std::uint64_t frame = 0;
    std::map<std::string, ParameterValue> values;
};
class EffectFrame {
public:
    // Same frame ID retries reuse its first sampled clock, including after an
    // aborted preparation. IDs increase after successful commit.
    static bool Begin(EffectPool&, FrameClock&, std::uint64_t frame, std::uint32_t tick,
                      float rawSeconds, bool edit, std::unique_ptr<EffectFrame>&,
                      std::string& reason);
    ~EffectFrame();
    bool Write(const EffectInstance&, const std::string&, const ParameterValue&,
               std::string& reason);
    // Later assignments see earlier staged shared writes. Returned values are
    // detached; changing the instance cannot change an already frozen draw.
    bool Prepare(const EffectInstance&, const SemanticContext&, ParameterSnapshot&,
                 std::string& reason);
    bool Commit(std::string& reason);
    bool Ready(std::string& reason) const;
    std::array<float, 4> Clock() const;

private:
    EffectFrame();
    std::unique_ptr<detail::FrameState> state_;
};
}
