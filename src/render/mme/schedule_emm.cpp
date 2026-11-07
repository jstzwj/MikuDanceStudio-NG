#include "schedule_internal.hpp"
#include <regex>
#include <set>
namespace mikudancestudio::mme {
using namespace detail;
std::uint64_t TargetScope(std::uint64_t effect, unsigned target) {
    return effect && effect < (std::uint64_t(1) << 48) && target < 255
               ? (effect << 8) | (target + 1)
               : 0;
}
namespace {
bool Key(const std::string& s, std::string& object, int& subset, bool& shown) {
    static const std::regex pattern(R"(^((?:Pmd|Acs)[0-9]+)(?:\[([0-9]+)\])?(\.show)?$)");
    std::smatch m;
    if (s.size() > 128 || !std::regex_match(s, m, pattern))
        return false;
    object = m[1];
    subset = m[2].matched ? std::stoi(m[2]) : -1;
    shown = m[3].matched;
    return true;
}
std::string TargetName(const std::string& section) {
    static const std::regex named(R"(^Effect@([A-Za-z_0-9]+)(?:\([0-9]+\))?$)");
    std::smatch match;
    if (!std::regex_match(section, match, named))
        ScheduleError("emm_section");
    return match[1];
}
std::string Base(const std::filesystem::path& p) {
    auto s = PathKey(p);
    return s.substr(s.find_last_of('/') == s.npos ? 0 : s.find_last_of('/') + 1);
}
}
bool ParseEmm(const std::string& text, EmmMapping& output, std::string& reason) try {
    if (text.size() > 4 * 1024 * 1024 || text.compare(0, 3, "\xef\xbb\xbf") == 0 ||
        text.find('\0') != text.npos)
        ScheduleError("emm_encoding_or_budget");
    EmmMapping next;
    std::map<std::string, std::set<std::string>> seen;
    std::set<std::string> sectionNames;
    std::string section, line;
    unsigned lineNo = 0;
    bool version = false;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (++lineNo > 65536 || line.size() > 1022)
            ScheduleError("emm_line_budget");
        line = line.substr(0, line.find_first_of(";#"));
        line = Trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section.find_first_of("[]") == section.npos && !section.empty()) {
                if (!sectionNames.insert(section).second)
                    ScheduleError("emm_duplicate_section");
                if (section.rfind("Effect@", 0) == 0)
                    TargetName(section);
                if (section == "Effect" || section.rfind("Effect@", 0) == 0)
                    next.sections.push_back({section});
                if (section != "Info" && section != "Object" && section != "Effect" &&
                    section.rfind("Effect@", 0) != 0)
                    ScheduleError("emm_section");
                continue;
            }
            ScheduleError("emm_section");
        }
        const auto eq = line.find('=');
        if (section.empty() || eq == line.npos)
            ScheduleError("emm_syntax/" + std::to_string(lineNo));
        const auto key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (key.empty() || key.find_first_of(" \t") != key.npos || value.empty() ||
            !seen[section].insert(key).second)
            ScheduleError("emm_duplicate_or_empty/" + key);
        if (section == "Info") {
            if (key != "Version" || (value != "1" && value != "2" && value != "3"))
                ScheduleError("emm_version");
            version = true;
        } else if (section == "Object") {
            std::string object;
            int subset;
            bool shown;
            if (!Key(key, object, subset, shown) || subset != -1 || shown)
                ScheduleError("emm_object_key");
            next.objects.push_back(
                {object, object.rfind("Acs", 0) == 0, std::filesystem::u8path(value)});
        } else {
            auto& target = *std::find_if(next.sections.begin(), next.sections.end(),
                                         [&](const auto& s) { return s.name == section; });
            if (key == "Default" && section == "Effect")
                target.effects[key] = value;
            else if (key == "Owner" && section != "Effect")
                target.owner = value;
            else {
                std::string object;
                int subset;
                bool shown;
                if (!Key(key, object, subset, shown))
                    ScheduleError("emm_assignment_key");
                if (shown) {
                    if (value != "true" && value != "false")
                        ScheduleError("emm_show");
                    target.shown[key.substr(0, key.size() - 5)] = value == "true";
                } else
                    target.effects[key] = value;
            }
        }
    }
    if (!version || !sectionNames.count("Object") || !sectionNames.count("Effect"))
        ScheduleError("emm_required_sections");
    std::set<std::string> objects;
    for (const auto& object : next.objects)
        objects.insert(object.key);
    for (const auto& s : next.sections) {
        if (s.name == "Effect" ? !s.effects.count("Default") : s.owner.empty())
            ScheduleError("emm_owner_or_default");
        for (const auto& [key, v] : s.effects)
            if (key != "Default") {
                std::string object;
                int subset;
                bool show;
                if (!Key(key, object, subset, show) || !objects.count(object))
                    ScheduleError("emm_unknown_object");
            }
        for (const auto& [key, v] : s.shown) {
            std::string object;
            int subset;
            bool show;
            if (!Key(key, object, subset, show) || !objects.count(object))
                ScheduleError("emm_unknown_object");
        }
    }
    output = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
bool ApplyEmm(const EmmMapping& mapping, const std::filesystem::path& directory,
              const std::vector<ScheduledObject>& objects,
              const std::vector<ScheduleDescription>& effects, AssignmentTable& output,
              std::string& reason) try {
    AssignmentTable next;
    std::map<std::string, const ScheduledObject*> matched;
    std::set<std::uint64_t> used;
    // Consume exact/tail path matches for all records before basename fallback.
    for (unsigned pass = 0; pass < 2; ++pass)
        for (const auto& record : mapping.objects) {
            if (matched.count(record.key))
                continue;
            auto path = RelativePath(directory, record.path.generic_u8string());
            const auto r = PathKey(path), raw = PathKey(record.path);
            for (const auto& object : objects)
                if (object.accessory == record.accessory && !used.count(object.id)) {
                    const auto p = PathKey(object.path);
                    const bool tail = p.size() > raw.size() &&
                                      p.compare(p.size() - raw.size(), raw.size(), raw) == 0 &&
                                      p[p.size() - raw.size() - 1] == '/';
                    if (pass == 0 ? (p == r || p == raw || tail)
                                  : Base(object.path) == Base(path)) {
                        matched[record.key] = &object;
                        used.insert(object.id);
                        break;
                    }
                }
        }
    if (matched.size() != mapping.objects.size())
        ScheduleError("emm_unmatched_objects");
    const auto choice = [&](const std::string& value) {
        if (value == "none")
            return Assignment{Assignment::Kind::Host};
        const auto key = PathKey(RelativePath(directory, value));
        for (const auto& e : effects)
            if (PathKey(e.binding.path) == key)
                return Assignment{Assignment::Kind::Effect, e.binding.id};
        ScheduleError("emm_effect_missing/" + value);
    };
    struct Scope {
        std::uint64_t id = 0, owner = 0;
        const TargetDeclaration* target = nullptr;
    };
    std::map<std::string, Scope> scopes;
    scopes["Effect"] = {};
    std::set<std::string> done;
    for (unsigned round = 0; round <= mapping.sections.size(); ++round) {
        bool progress = false;
        for (const auto& section : mapping.sections) {
            if (done.count(section.name))
                continue;
            std::uint64_t scope = 0;
            Scope context;
            if (section.name != "Effect") {
                const auto at = section.owner.find('@');
                const auto parent =
                    at == section.owner.npos ? "Effect" : "Effect@" + section.owner.substr(at + 1);
                if (!done.count(parent))
                    continue;
                std::string object;
                int subset;
                bool show;
                if (!Key(section.owner.substr(0, at), object, subset, show) || show ||
                    !matched.count(object))
                    ScheduleError("emm_owner");
                Assignment assignment;
                const auto* host = matched.at(object);
                auto owner = *host;
                owner.visible = true;
                owner.sceneCarrier = false;
                auto bindingsOnly = next;
                bindingsOnly.shown.clear();
                const auto& inherited = scopes.at(parent);
                const std::vector<std::pair<std::string, std::string>> empty;
                if (!ResolveAssignment(owner, subset < 0 ? 0 : subset, inherited.id,
                                       inherited.owner,
                                       inherited.target ? inherited.target->defaults : empty,
                                       bindingsOnly, effects, assignment, reason) ||
                    assignment.kind != Assignment::Kind::Effect)
                    ScheduleError("emm_owner_effect");
                const auto& effect =
                    *std::find_if(effects.begin(), effects.end(),
                                  [&](const auto& e) { return e.binding.id == assignment.effect; });
                const auto target = TargetName(section.name);
                for (unsigned i = 0; i < effect.targets.size(); ++i)
                    if (effect.targets[i].target.offscreen && effect.targets[i].name == target) {
                        scope = TargetScope(effect.binding.id, i);
                        context = {scope, host->id, &effect.targets[i]};
                    }
                if (!scope)
                    ScheduleError("emm_owner_without_target");
            }
            for (const auto& [key, value] : section.effects) {
                if (key == "Default") {
                    next.fallback = choice(value);
                    continue;
                }
                std::string object;
                int subset;
                bool show;
                if (!Key(key, object, subset, show) || !matched.count(object))
                    ScheduleError("emm_unknown_object");
                if (subset >= int(matched.at(object)->flags.size()))
                    continue;
                next.effects[{scope, matched.at(object)->id, subset}] = choice(value);
            }
            for (const auto& [key, value] : section.shown) {
                std::string object;
                int subset;
                bool show;
                Key(key, object, subset, show);
                if (subset >= int(matched.at(object)->flags.size()))
                    continue;
                next.shown[{scope, matched.at(object)->id, subset}] = value;
            }
            scopes[section.name] = context;
            done.insert(section.name);
            progress = true;
        }
        if (done.size() == mapping.sections.size())
            break;
        if (!progress)
            ScheduleError("emm_owner_cycle");
    }
    output = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
}
