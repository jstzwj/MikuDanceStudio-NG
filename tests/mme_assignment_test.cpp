#include "mme_schedule_fixture.hpp"
#include "mme_assignment_rules.h"
using namespace mme_test;
int main() try {
    EffectPool pool;
    auto input = ScheduleFixture(pool);
    std::string reason;
    auto& material = input.effects[0];
    std::map<int, int> ranges;
    Check(mme_rules::SubsetRanges(ranges, "0-2,5,7-") && ranges.at(0) == 2 && ranges.at(5) == 5 &&
              ranges.at(7) == INT32_MAX,
          "original subset range grammar");
    Check(mme_rules::SubsetRanges(ranges, "") && ranges.begin()->first == INT32_MAX,
          "original empty range sentinel");
    for (const auto* s : {"-1", "2--3", "3q", "2147483648", "99999999999999999999999999"})
        Check(!mme_rules::SubsetRanges(ranges, s), "invalid or nonportable subset range rejected");
    const auto glob = [](const std::string& name, const std::string& pattern) {
        return mme_rules::WildcardMatch(name.c_str(), name.size(), pattern.c_str(), pattern.size(),
                                        true);
    };
    Check(glob("skyBLUEbox.pmx", "sky*box.*") && glob("A*.pmx", "a\\*.pmx") &&
              !glob("abc", "abc\\") && glob("", "***") && !glob("", "?"),
          "original wildcard collapse, escape and empty-name behavior");
    int selected = -1;
    Check(SelectTechnique(material, 0, 1, 1, {true, true, true}, selected, reason) && selected == 0,
          "texture/subset chooses first valid technique");
    Check(SelectTechnique(material, 0, 3, 1, {true, true, true}, selected, reason) && selected == 1,
          "out of range selects fallback technique");
    Check(SelectTechnique(material, 0, 0, 1, {false, true, true}, selected, reason) &&
              selected == 1,
          "invalid match does not hide later valid match");
    Check(!SelectTechnique(material, 0, 0, 1, {false, false, true}, selected, reason),
          "all matching techniques invalid rejects assignment");
    Check(SelectTechnique(material, 3, 0, 0, {true, true, true}, selected, reason) && selected == 2,
          "explicit empty edge technique is selected");
    Check(SelectTechnique(material, 4, 0, 0, {true, true, true}, selected, reason) &&
              selected == -1,
          "absent technique forwards host draw");
    Check(SelectTechnique(material, 0, -1, 0, {true, true, true}, selected, reason) &&
              selected == -1,
          "negative subset forwards host");
    auto filtered = material;
    filtered.techniques[0].use = {1, 1, 0};
    Check(SelectTechnique(filtered, 0, 0, 3, {true, true, true}, selected, reason) && selected == 0,
          "sphere flag participates");
    Check(SelectTechnique(filtered, 0, 0, 7, {true, true, true}, selected, reason) && selected == 1,
          "toon flag participates");
    filtered.techniques[0].allSubsets = false;
    filtered.techniques[0].subsets = {{INT32_MAX, INT32_MAX}};
    Check(SelectTechnique(filtered, 0, 0, 3, {true, true, true}, selected, reason) && selected == 1,
          "empty Subset hides every real subset");
    AssignmentTable table;
    Assignment result;
    auto object = input.objects.back();
    object.path = material.binding.path;
    object.path.replace_extension(".pmx");
    const auto resolve = [&](int subset, std::uint64_t scope, std::uint64_t owner,
                             const std::vector<std::pair<std::string, std::string>>& defaults) {
        Check(ResolveAssignment(object, subset, scope, owner, defaults, table, input.effects,
                                result, reason),
              reason);
    };
    resolve(0, 0, 0, {});
    Check(result.effect == 10, "same-name automatic assignment");
    table.effects[{0, object.id, -1}] = {Assignment::Kind::Host};
    resolve(0, 0, 0, {});
    Check(result.kind == Assignment::Kind::Host, "explicit none suppresses automatic lookup");
    table.effects[{0, object.id, 1}] = {Assignment::Kind::Effect, 11};
    resolve(1, 0, 0, {});
    Check(result.effect == 11, "subset overrides whole object");
    table.shown[{0, object.id, -1}] = false;
    table.shown[{0, object.id, 1}] = true;
    resolve(1, 0, 0, {});
    Check(result.effect == 11, "subset show takes precedence");
    resolve(0, 0, 0, {});
    Check(result.kind == Assignment::Kind::Hidden, "whole show gate");
    table.shown.clear();
    table.effects.clear();
    object.visible = false;
    resolve(0, 33, 999, {{"*", "none"}});
    Check(result.kind == Assignment::Kind::Host,
          "offscreen visibility is independent of base visibility");
    resolve(0, 33, 999, {{"*", "main_default"}});
    Check(result.kind == Assignment::Kind::Hidden, "main_default inherits base visibility");
    table.shown[{33, object.id, 0}] = true;
    resolve(0, 33, 999, {{"*", "main_default"}});
    Check(result.effect == 10, "explicit offscreen show overrides base visibility");
    object.visible = true;
    object.sceneCarrier = true;
    table.shown.clear();
    resolve(0, 33, 999, {{"*", "none"}});
    Check(result.kind == Assignment::Kind::Hidden, "scene carriers default hidden offscreen");
    object.sceneCarrier = false;
    resolve(0, 33, object.id, {{"self", "hide"}, {"*", "none"}});
    Check(result.kind == Assignment::Kind::Hidden, "self precedes wildcard");
    resolve(0, 33, 999, {{"material.?mx", "none"}, {"*", "hide"}});
    Check(result.kind == Assignment::Kind::Host, "case-insensitive wildcard and question mark");
    resolve(0, 33, 999, {});
    Check(result.kind == Assignment::Kind::Host, "absent DefaultEffect uses raw host draw");
    resolve(0, 33, 999, {{"*", "main_default"}});
    Check(result.effect == 10, "main_default resolves ordinary assignment");
    object.path = material.binding.path.parent_path() / "name[Material.fx].pmx";
    resolve(0, 0, 0, {});
    Check(result.effect == 10, "embedded filename convention");
    object.path = "/unassigned/model.pmx";
    table.fallback = {Assignment::Kind::Effect, 11};
    resolve(0, 0, 0, {});
    Check(result.effect == 11, "EMM default is final fallback");
    table.effects[{0, object.id, -1}] = {Assignment::Kind::Effect, 999};
    Check(!ResolveAssignment(object, 0, 0, 0, {}, table, input.effects, result, reason),
          "stale explicit assignment rejected");

    // EMM records match paths and type, not their serialized slot numbers.
    const auto directory = material.binding.path.parent_path();
    std::vector<ScheduledObject> objects = {{40, directory / "stage.x", 0, true, true, {0}},
                                            {50, directory / "leaf.pmx", 1, false, true, {1, 0}},
                                            {60, directory / "other.pmx", 2, false, true, {0}}};
    const std::string emm =
        "[Info]\r\nVersion=3\r\n[Object]\r\nAcs7=stage.x\r\nPmd9=leaf.pmx\r\nPmd1=other.pmx\r\n[Effect]\r\nDefault=Material.fx\r\nAcs7=OuterScene.fx\r\nPmd9=none\r\nPmd9[1]=ReaderMaterial.fx\r\nPmd9.show=false\r\nPmd9[1].show=true\r\n[Effect@ParentMap]\r\nOwner=Acs7\r\nPmd9=Child.fx\r\n[Effect@ChildMap]\r\nOwner=Pmd9@ParentMap\r\nPmd9=Material.fx\r\n";
    EmmMapping mapping;
    Check(ParseEmm(emm, mapping, reason), reason);
    Check(ApplyEmm(mapping, directory, objects, input.effects, table, reason), reason);
    Check(table.effects.at({0, 40, -1}).effect == 12 && table.fallback.effect == 10,
          "Default never overwrites accessory assignment");
    Check(table.effects.at({0, 50, -1}).kind == Assignment::Kind::Host &&
              table.effects.at({0, 50, 1}).effect == 14,
          "explicit none and subset import");
    Check(!table.shown.at({0, 50, -1}) && table.shown.at({0, 50, 1}), "show-only subset import");
    Check(table.effects.at({TargetScope(12, 0), 50, -1}).effect == 11 &&
              table.effects.at({TargetScope(11, 0), 50, -1}).effect == 10,
          "named resource and nested Owner scopes");
    auto multi = input.effects;
    auto extra = multi[2].targets[0];
    extra.name = "OtherMap";
    extra.target.name = extra.name;
    multi[2].targets.push_back(extra);
    Check(ApplyEmm(mapping, directory, objects, multi, table, reason) &&
              table.effects.at({TargetScope(12, 0), 50, -1}).effect == 11,
          "named section selects target on a multi-target owner");
    auto bad = mapping;
    bad.sections[2].owner = "Pmd9@ChildMap";
    Check(!ApplyEmm(bad, directory, objects, input.effects, table, reason) &&
              table.effects.at({0, 40, -1}).effect == 12,
          "Owner cycle atomic failure");
    bad = mapping;
    bad.sections[1].name = "Effect@Missing";
    Check(!ApplyEmm(bad, directory, objects, input.effects, table, reason),
          "Owner target name must exist");
    Check(!ParseEmm(emm + "Pmd9=none\n", mapping, reason), "duplicate keys rejected");
    Check(!ParseEmm(emm + "[Effect]\n", mapping, reason), "duplicate sections rejected");
    Check(!ParseEmm("\xef\xbb\xbf" + emm, mapping, reason),
          "BOM explicitly rejected before legacy parser");
    auto stencil =
        std::make_shared<EffectDefinition>(input.effects[3].binding.instance->Definition());
    stencil->metadata.techniques[0].annotations["SCRIPT"] = "Clear=Stencil;";
    auto binding = input.effects[3].binding;
    binding.instance.reset();
    Check(EffectInstance::Create(stencil, pool, binding.instance, reason), reason);
    ScheduleDescription unsupported;
    Check(!DescribeSchedule(binding, unsupported, reason) &&
              reason.find("stencil_clear_not_supported") != reason.npos,
          "unsupported stencil clear is not converted into depth clear");
    std::cout
        << "Technique selection, assignment precedence, EMM mapping/owners and atomic errors passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
