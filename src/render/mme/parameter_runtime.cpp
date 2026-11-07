#include "parameter_runtime_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

namespace mikudancestudio::mme {

namespace {
bool Fail(std::string& reason, const std::string& code) {
    reason = "MME_runtime/" + code;
    return false;
}
bool Live(const detail::Cell& cell) {
    return std::any_of(cell.consumers.begin(), cell.consumers.end(),
                       [](const auto& c) { return !c.expired(); });
}
void Register(detail::Cell& cell, const std::shared_ptr<detail::InstanceState>& state) {
    cell.consumers.erase(std::remove_if(cell.consumers.begin(), cell.consumers.end(),
                                        [](const auto& c) { return c.expired(); }),
                         cell.consumers.end());
    cell.consumers.push_back(state);
}
}
bool detail::Compatible(const ParameterValue& a, const ParameterValue& b) {
    if (a.type != b.type || a.rows != b.rows || a.columns != b.columns ||
        a.elements != b.elements || a.array != b.array || a.words.size() != b.words.size())
        return false;
    if (b.type == ParameterValue::Type::String)
        return b.text.size() <= 65536 && b.words.empty();
    if (!b.text.empty())
        return false;
    for (auto word : b.words) {
        if (b.type == ParameterValue::Type::Float) {
            float f;
            std::memcpy(&f, &word, 4);
            if (!std::isfinite(f))
                return false;
        }
        if (b.type == ParameterValue::Type::Bool && word > 1)
            return false;
    }
    return true;
}

EffectPool::EffectPool() : state_(std::make_shared<detail::PoolState>()) {
}
FrameClock::FrameClock() : state_(std::make_shared<detail::ClockState>()) {
}
bool EffectInstance::Create(std::shared_ptr<const EffectDefinition> definition, EffectPool& pool,
                            std::shared_ptr<EffectInstance>& output, std::string& reason) {
    if (!definition)
        return Fail(reason, "missing_definition");
    std::map<std::string, ParameterValue> defaults;
    if (!EvaluateDefaults(*definition, defaults, reason))
        return false;
    auto next = std::shared_ptr<EffectInstance>(new EffectInstance);
    next->state_ = std::make_shared<detail::InstanceState>();
    auto& s = *next->state_;
    s.pool = pool.state_;
    s.definition = std::move(definition);
    // No pool mutation until the entire definition has initialized correctly.
    std::vector<std::pair<detail::Key, std::shared_ptr<detail::Cell>>> publish;
    for (const auto& v : s.definition->metadata.variables) {
        if (v.immutable)
            continue;
        auto cell = std::make_shared<detail::Cell>();
        cell->value = defaults.at(v.name);
        cell->immutable = v.immutable;
        if (v.shared && !v.immutable) {
            const auto& value = cell->value;
            detail::Key key{v.name,        v.semantic,     value.type, value.rows,
                            value.columns, value.elements, value.array};
            auto found = s.pool->shared.find(key);
            auto existing = found == s.pool->shared.end() ? nullptr : found->second.lock();
            if (existing && Live(*existing))
                cell = std::move(existing);
            else
                publish.emplace_back(std::move(key), cell);
            Register(*cell, next->state_);
        }
        s.cells.emplace(v.name, std::move(cell));
    }
    // Prune dead keys: repeated hot reload must not grow the pool indefinitely.
    for (auto it = s.pool->shared.begin(); it != s.pool->shared.end();) {
        const auto cell = it->second.lock();
        if (!cell || !Live(*cell))
            it = s.pool->shared.erase(it);
        else
            ++it;
    }
    for (auto& item : publish)
        s.pool->shared[item.first] = item.second;
    ++s.pool->revision;
    output = std::move(next);
    reason.clear();
    return true;
}
const EffectDefinition& EffectInstance::Definition() const {
    return *state_->definition;
}
std::shared_ptr<const EffectDefinition> EffectInstance::DefinitionSnapshot() const {
    return state_->definition;
}
bool EffectInstance::Clone(std::shared_ptr<EffectInstance>& output, std::string& reason) const {
    auto next = std::shared_ptr<EffectInstance>(new EffectInstance);
    next->state_ = std::make_shared<detail::InstanceState>(*state_);
    for (const auto& v : state_->definition->metadata.variables) {
        if (!v.immutable && !v.shared)
            next->state_->cells[v.name] = std::make_shared<detail::Cell>(*state_->cells.at(v.name));
        else if (!v.immutable)
            Register(*next->state_->cells.at(v.name), next->state_);
    }
    ++state_->pool->revision;
    output = std::move(next);
    reason.clear();
    return true;
}
bool EffectInstance::Read(const std::string& name, ParameterValue& output,
                          std::string& reason) const {
    const auto found = state_->cells.find(name);
    if (found == state_->cells.end())
        return Fail(reason, name + "/missing_parameter");
    output = found->second->value;
    reason.clear();
    return true;
}
bool EffectInstance::Write(const std::string& name, const ParameterValue& value,
                           std::string& reason) {
    const auto found = state_->cells.find(name);
    if (found == state_->cells.end())
        return Fail(reason, name + "/missing_parameter");
    if (found->second->immutable)
        return Fail(reason, name + "/immutable");
    if (!detail::Compatible(found->second->value, value))
        return Fail(reason, name + "/type_shape_or_value");
    found->second->value = value;
    ++state_->pool->revision;
    reason.clear();
    return true;
}
}
