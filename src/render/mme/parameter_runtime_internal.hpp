#pragma once
#include "mikudancestudio/mme_runtime.hpp"
#include <tuple>
#include <optional>
namespace mikudancestudio::mme {
namespace detail {
struct Cell {
    ParameterValue value;
    bool immutable = false;
    // Pending frames can retain storage after its last real consumer unloads.
    std::vector<std::weak_ptr<InstanceState>> consumers;
};
using Key =
    std::tuple<std::string, std::string, ParameterValue::Type, unsigned, unsigned, unsigned, bool>;
bool Compatible(const ParameterValue&, const ParameterValue&);
bool EvaluateSemanticsForFrame(const EffectDefinition&, const SemanticContext&,
                               const MmeStandardInputs&, std::map<std::string, ParameterValue>&,
                               std::string&);
struct PoolState {
    std::uint64_t revision = 0;
    std::map<Key, std::weak_ptr<Cell>> shared;
};
struct InstanceState {
    std::shared_ptr<const EffectDefinition> definition;
    std::shared_ptr<PoolState> pool;
    std::map<std::string, std::shared_ptr<Cell>> cells;
};
struct ClockState {
    MmeFrameClock committed, sampled;
    std::optional<std::uint64_t> committedId, sampledId;
    std::uint64_t revision = 0;
};
struct FrameState {
    std::shared_ptr<PoolState> pool;
    std::shared_ptr<ClockState> clock;
    std::uint64_t id = 0, poolRevision = 0, clockRevision = 0;
    MmeFrameClock sampled;
    bool closed = false;
    // Shared ownership only for staged cells; weak instance guards still
    // reject deletion of an assignment while preparation is in flight.
    std::map<std::shared_ptr<Cell>, ParameterValue, std::owner_less<std::shared_ptr<Cell>>> writes;
    std::vector<std::weak_ptr<InstanceState>> instances;
};
}
}
