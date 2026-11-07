#include "parameter_runtime_internal.hpp"
#include <cmath>
#include <cstring>
namespace mikudancestudio::mme {
namespace {
bool Fail(std::string& reason, const std::string& code) {
    reason = "MME_runtime/" + code;
    return false;
}
bool Current(const detail::FrameState& f, std::string& reason) {
    if (f.closed)
        return Fail(reason, "frame_closed");
    if (f.poolRevision != f.pool->revision || f.clockRevision != f.clock->revision ||
        f.clock->sampledId != f.id)
        return Fail(reason, "stale_preparation");
    for (const auto& instance : f.instances)
        if (instance.expired())
            return Fail(reason, "expired_instance");
    return true;
}
}
EffectFrame::EffectFrame() = default;
EffectFrame::~EffectFrame() = default;
bool EffectFrame::Begin(EffectPool& pool, FrameClock& clock, std::uint64_t id, std::uint32_t tick,
                        float raw, bool edit, std::unique_ptr<EffectFrame>& output,
                        std::string& reason) {
    auto& c = *clock.state_;
    if (!std::isfinite(raw) || (c.committedId && id <= *c.committedId) ||
        (c.sampledId && id < *c.sampledId))
        return Fail(reason, "frame_id_or_clock");
    if (c.sampledId != id) {
        auto sampled = c.committed;
        MmeAdvanceFrameClock(sampled, tick, raw, edit);
        if (!std::isfinite(sampled.time) || !std::isfinite(sampled.delta) ||
            !std::isfinite(sampled.rawDelta))
            return Fail(reason, "clock_overflow");
        c.sampled = sampled;
        c.sampledId = id;
    }
    auto next = std::unique_ptr<EffectFrame>(new EffectFrame);
    next->state_ = std::make_unique<detail::FrameState>();
    auto& s = *next->state_;
    s.pool = pool.state_;
    s.clock = clock.state_;
    s.id = id;
    s.poolRevision = pool.state_->revision;
    s.clockRevision = c.revision;
    s.sampled = c.sampled;
    output = std::move(next);
    reason.clear();
    return true;
}
std::array<float, 4> EffectFrame::Clock() const {
    const auto& c = state_->sampled;
    return {c.time, c.delta, c.raw, c.rawDelta};
}
bool EffectFrame::Write(const EffectInstance& instance, const std::string& name,
                        const ParameterValue& value, std::string& reason) {
    auto& s = *state_;
    if (!Current(s, reason))
        return false;
    if (instance.state_->pool != s.pool)
        return Fail(reason, "foreign_pool");
    const auto found = instance.state_->cells.find(name);
    if (found == instance.state_->cells.end())
        return Fail(reason, name + "/missing_parameter");
    if (found->second->immutable || !detail::Compatible(found->second->value, value))
        return Fail(reason, name + "/immutable_type_shape_or_value");
    s.writes[found->second] = value;
    s.instances.push_back(instance.state_);
    reason.clear();
    return true;
}
bool EffectFrame::Prepare(const EffectInstance& instance, const SemanticContext& context,
                          ParameterSnapshot& output, std::string& reason) {
    auto& s = *state_;
    if (!Current(s, reason))
        return false;
    if (instance.state_->pool != s.pool)
        return Fail(reason, "foreign_pool");
    auto inputs = context.standard;
    inputs.clock = Clock();
    inputs.clockReady = true;
    std::map<std::string, ParameterValue> semantic;
    if (!detail::EvaluateSemanticsForFrame(*instance.state_->definition, context, inputs, semantic,
                                           reason))
        return false;
    // Stage on a copy, preserving the entire transaction if any binding fails.
    auto pending = s.writes;
    for (const auto& [name, value] : semantic) {
        const auto cell = instance.state_->cells.at(name);
        if (cell->immutable || !detail::Compatible(cell->value, value))
            return Fail(reason, name + "/semantic_shape");
        pending[cell] = value;
    }
    ParameterSnapshot next;
    next.definition = instance.state_->definition;
    next.frame = s.id;
    for (const auto& [name, cell] : instance.state_->cells) {
        auto staged = pending.find(cell);
        next.values.emplace(name, staged == pending.end() ? cell->value : staged->second);
    }
    s.writes = std::move(pending);
    s.instances.push_back(instance.state_);
    output = std::move(next);
    reason.clear();
    return true;
}
bool EffectFrame::Commit(std::string& reason) {
    auto& s = *state_;
    if (!Current(s, reason))
        return false;
    for (auto& [cell, value] : s.writes)
        cell->value = std::move(value);
    ++s.pool->revision;
    s.clock->committed = s.sampled;
    s.clock->committedId = s.id;
    ++s.clock->revision;
    s.closed = true;
    reason.clear();
    return true;
}
bool EffectFrame::Ready(std::string& reason) const {
    return Current(*state_, reason);
}
}
