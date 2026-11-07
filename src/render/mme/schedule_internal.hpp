#pragma once
#include "mikudancestudio/mme_schedule.hpp"
#include "mme_assignment_rules.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>
#include <locale>
#include <stdexcept>
namespace mikudancestudio::mme::detail {
[[noreturn]] inline void ScheduleError(const std::string& reason) {
    throw std::runtime_error("MME_schedule/" + reason);
}
inline std::string Upper(std::string s) {
    for (auto& c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
inline std::string Trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\v\r\n");
    return a == s.npos ? "" : s.substr(a, s.find_last_not_of(" \t\v\r\n") - a + 1);
}
inline std::string PathKey(const std::filesystem::path& path) {
    auto s = path.generic_u8string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return Upper(std::filesystem::u8path(s).lexically_normal().generic_u8string());
}
inline std::filesystem::path RelativePath(const std::filesystem::path& base,
                                          const std::string& text) {
    auto s = text;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.size() > 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' &&
        s[2] == '/')
        return std::filesystem::u8path(s).lexically_normal();
    return (base / std::filesystem::u8path(s)).lexically_normal();
}
inline std::vector<float> Numbers(std::string text, unsigned count) {
    for (auto& c : text)
        if (c == '{' || c == '}' || c == ',')
            c = ' ';
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::vector<float> result(count);
    for (auto& n : result)
        if (!(in >> n) || !std::isfinite(n))
            ScheduleError("annotation_number");
    std::string rest;
    if (in >> rest)
        ScheduleError("annotation_number_tail");
    return result;
}
struct TargetRegistryState {
    struct Entry {
        std::string key;
        rhi::FrameTarget target;
        TargetLease lease;
    };
    std::uint64_t revision = 0, epoch = 1, nextId = 16, device = 0;
    unsigned width = 0, height = 0;
    std::map<std::string, Entry> entries;
    std::map<std::pair<std::uint64_t, std::string>, std::string> bindings;
    std::map<std::uint64_t, std::uint64_t> effects;
};
struct ScheduleState {
    std::shared_ptr<TargetRegistryState> registry;
    std::uint64_t revision = 0;
    std::unique_ptr<EffectFrame> transaction;
    std::vector<PlannedStep> steps;
    rhi::RenderFrame dependencies;
    std::map<std::uint64_t, std::uint64_t> effects;
    std::map<std::pair<std::uint64_t, std::string>, TargetLease> bindings;
};
}
