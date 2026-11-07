#include "schedule_internal.hpp"
namespace mikudancestudio::mme {
using namespace detail;
namespace {
std::uint64_t FindEffect(const std::vector<ScheduleDescription>& effects,
                         const std::filesystem::path& path) {
    for (const auto& e : effects)
        if (PathKey(e.binding.path) == PathKey(path))
            return e.binding.id;
    return 0;
}
Assignment Value(const std::string& value, const std::vector<ScheduleDescription>& effects) {
    if (value == "none")
        return {Assignment::Kind::Host};
    if (value == "hide")
        return {Assignment::Kind::Hidden};
    if (value == "main_default")
        return {Assignment::Kind::MainDefault};
    const auto id = FindEffect(effects, std::filesystem::u8path(value));
    if (!id)
        ScheduleError("effect_not_loaded/" + value);
    return {Assignment::Kind::Effect, id};
}
}
static bool ResolveImpl(const ScheduledObject& object, int subset, std::uint64_t scope,
                        std::uint64_t owner,
                        const std::vector<std::pair<std::string, std::string>>& defaults,
                        const AssignmentTable& table,
                        const std::vector<ScheduleDescription>& effects, Assignment& output,
                        std::string& reason, bool ignoreVisibility) try {
    Assignment next;
    bool resolved = false;
    const auto key = AssignmentKey{scope, object.id, subset},
               whole = AssignmentKey{scope, object.id, -1};
    auto shown = table.shown.find(key);
    if (shown == table.shown.end())
        shown = table.shown.find(whole);
    auto explicitValue = table.effects.find(key);
    if (explicitValue == table.effects.end())
        explicitValue = table.effects.find(whole);
    if (explicitValue != table.effects.end() &&
        explicitValue->second.kind != Assignment::Kind::Automatic) {
        next = explicitValue->second;
        resolved = true;
    }
    if (!resolved && scope) {
        auto base = object.path.generic_u8string();
        std::replace(base.begin(), base.end(), '\\', '/');
        base = base.substr(base.find_last_of('/') == base.npos ? 0 : base.find_last_of('/') + 1);
        if (base.size() > 4096)
            ScheduleError("object_name_budget");
        for (const auto& row : defaults) {
            const auto& key = row.first;
            if (key.size() > 512)
                ScheduleError("pattern_budget");
            if (key == "self" ? object.id == owner
                              : mme_rules::WildcardMatch(base.c_str(), base.size(), key.c_str(),
                                                         key.size(), true)) {
                next = Value(row.second, effects);
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            next.kind = Assignment::Kind::Host;
            resolved = true;
        }
    }
    if (next.kind == Assignment::Kind::MainDefault) {
        if (!scope)
            ScheduleError("recursive_main_default");
        if (!ResolveImpl(object, subset, 0, 0, {}, table, effects, next, reason, true))
            return false;
        if (!ignoreVisibility && shown == table.shown.end()) {
            auto base = table.shown.find({0, object.id, subset});
            if (base == table.shown.end())
                base = table.shown.find({0, object.id, -1});
            if (base == table.shown.end() ? !object.visible : !base->second)
                next.kind = Assignment::Kind::Hidden;
        }
    }
    if (!resolved) {
        auto path = object.path;
        path.replace_extension(".fx");
        auto id = FindEffect(effects, path);
        if (!id) {
            const auto stem = object.path.stem().generic_u8string();
            const auto open = stem.find('['), close = stem.find(']', open);
            if (open != stem.npos && close != stem.npos && close > open + 1)
                id = FindEffect(effects, RelativePath(object.path.parent_path(),
                                                      stem.substr(open + 1, close - open - 1)));
        }
        next = id ? Assignment{Assignment::Kind::Effect, id} : table.fallback;
        if (next.kind != Assignment::Kind::Host && next.kind != Assignment::Kind::Effect)
            ScheduleError("invalid_default_assignment");
    }
    if (next.kind == Assignment::Kind::Effect &&
        std::none_of(effects.begin(), effects.end(),
                     [&](const auto& e) { return e.binding.id == next.effect; }))
        ScheduleError("stale_assignment");
    if (!ignoreVisibility) {
        if (shown != table.shown.end()) {
            if (!shown->second)
                next.kind = Assignment::Kind::Hidden;
            else if (next.kind == Assignment::Kind::Hidden)
                next.kind = Assignment::Kind::Host;
        } else if (scope ? object.sceneCarrier : !object.visible)
            next.kind = Assignment::Kind::Hidden;
    }
    output = next;
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
bool ResolveAssignment(const ScheduledObject& object, int subset, std::uint64_t scope,
                       std::uint64_t owner,
                       const std::vector<std::pair<std::string, std::string>>& defaults,
                       const AssignmentTable& table,
                       const std::vector<ScheduleDescription>& effects, Assignment& output,
                       std::string& reason) {
    return ResolveImpl(object, subset, scope, owner, defaults, table, effects, output, reason,
                       false);
}
}
