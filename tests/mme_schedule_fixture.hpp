#pragma once
#include "mme_runtime_test_support.hpp"
#include "mikudancestudio/mme_schedule.hpp"
#include <fstream>
#include <iterator>
namespace mme_test {
inline ScheduleDescription Scheduled(EffectPool& pool, std::uint64_t id, const std::string& name,
                                     const std::filesystem::path& dxc = {}) {
    const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "assets/effects/schedule" / name;
    std::ifstream file(path);
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    Check(!text.empty(), "fixture " + name);
    std::shared_ptr<const EffectDefinition> def;
    std::string reason;
    if (!dxc.empty()) {
        SourceOptions options;
        options.preprocessor = dxc;
        Check(LoadEffectDefinition(path, options, def, reason), reason);
    } else {
        auto raw = std::make_shared<EffectDefinition>();
        if (!MmeLegacyDescribe(text, raw->metadata, reason))
            Check(false, name + ": " + reason);
        def = std::move(raw);
    }
    std::shared_ptr<EffectInstance> instance;
    Check(EffectInstance::Create(def, pool, instance, reason), reason);
    ScheduleDescription output;
    if (!DescribeSchedule({id, 1, path, instance}, output, reason))
        Check(false, name + ": " + reason);
    return output;
}
inline ScheduleInput ScheduleFixture(EffectPool& pool, const std::filesystem::path& dxc = {}) {
    ScheduleInput input;
    for (const auto& item :
         std::vector<std::pair<std::uint64_t, std::string>>{{10, "Material.fx"},
                                                            {11, "Child.fx"},
                                                            {12, "OuterScene.fx"},
                                                            {13, "InnerScene.fx"},
                                                            {14, "ReaderMaterial.fx"}})
        input.effects.push_back(Scheduled(pool, item.first, item.second, dxc));
    input.objects = {{1, "/models/outer.x", 30, true, true, {0}, true},
                     {2, "/models/inner.x", 20, true, true, {0}, true},
                     {3, "/models/leaf.pmx", 10, false, true, {1}}};
    input.assignments.effects[{0, 1, -1}] = {Assignment::Kind::Effect, 12};
    input.assignments.effects[{0, 2, -1}] = {Assignment::Kind::Effect, 13};
    input.assignments.effects[{0, 3, -1}] = {Assignment::Kind::Effect, 14};
    for (const auto& e : input.effects)
        input.techniqueValidity[e.binding.id] = std::vector<bool>(e.techniques.size(), true);
    input.context = [](auto, int, auto) { return Context(); };
    return input;
}
}
