#include "schedule_internal.hpp"
namespace mikudancestudio::mme {
using namespace detail;
namespace {
std::vector<MmePortableCommand> Commands(const std::string& text, const MmeLegacyProgram& p,
                                         const MmeLegacyTechnique& technique) {
    if (text.size() > 65536)
        ScheduleError("script_budget");
    std::vector<MmePortableCommand> result;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = text.find(';', at);
        if (end == text.npos) {
            if (Trim(text.substr(at)).empty())
                break;
            ScheduleError("script_terminator");
        }
        const auto token = Trim(text.substr(at, end - at));
        at = end + 1;
        const auto eq = token.find('=');
        if (eq == token.npos || token.find('=', eq + 1) != token.npos)
            ScheduleError("script_syntax");
        const auto name = Upper(Trim(token.substr(0, eq))), value = Trim(token.substr(eq + 1));
        if (value.find_first_of(" \t\r\n") != value.npos)
            ScheduleError("script_value_whitespace");
        using K = MmePortableCommand::Kind;
        MmePortableCommand c{K::Pass};
        c.parameter = value;
        if (name == "PASS") {
            bool found = false;
            for (unsigned local = 0; local < technique.passes.size(); ++local)
                if (p.passes.at(technique.passes[local]).name == value) {
                    c.value = int(local);
                    found = true;
                    break;
                }
            if (!found)
                ScheduleError("unknown_pass/" + value);
            c.parameter.clear();
        } else if (name == "RENDERCOLORTARGET") {
            c.kind = K::ColorTarget;
        } else if (name.size() == 18 && name.substr(0, 17) == "RENDERCOLORTARGET" &&
                   name.back() >= '0' && name.back() <= '3') {
            c.kind = K::ColorTarget;
            c.value = name.back() - '0';
        } else if (name == "RENDERDEPTHSTENCILTARGET")
            c.kind = K::DepthTarget;
        else if (name == "CLEARSETCOLOR")
            c.kind = K::ClearColorValue;
        else if (name == "CLEARSETDEPTH")
            c.kind = K::ClearDepthValue;
        else if (name == "CLEAR") {
            c.kind = K::Clear;
            c.parameter.clear();
            if (Upper(value) == "COLOR")
                c.value = 1;
            else if (Upper(value) == "DEPTH")
                c.value = 2;
            else if (Upper(value) == "STENCIL")
                c.value = 4;
            else
                ScheduleError("clear_type");
        } else if (name == "SCRIPTEXTERNAL" && Upper(value) == "COLOR") {
            c.kind = K::Capture;
            c.parameter.clear();
        } else if (name == "LOOPBYCOUNT")
            c.kind = K::Loop;
        else if (name == "LOOPGETINDEX")
            c.kind = K::LoopIndex;
        else if (name == "LOOPEND" && value.empty())
            c.kind = K::LoopEnd;
        else
            ScheduleError("unsupported_command/" + name);
        result.push_back(std::move(c));
        if (result.size() > 256)
            ScheduleError("script_budget");
    }
    return result;
}
}
bool DescribeSchedule(const EffectBinding& binding, ScheduleDescription& output,
                      std::string& reason) try {
    if (!binding.id || !binding.revision || !binding.instance || binding.path.empty())
        ScheduleError("binding_identity");
    ScheduleDescription next;
    next.binding = binding;
    const auto& p = binding.instance->Definition().metadata;
    for (const auto& v : p.variables)
        if (v.semantic == "STANDARDSGLOBAL") {
            const auto cls = v.annotations.find("SCRIPTCLASS"),
                       order = v.annotations.find("SCRIPTORDER");
            if (cls != v.annotations.end()) {
                const auto value = Upper(cls->second);
                if (value != "OBJECT" && value != "SCENE" && value != "SCENEOROBJECT")
                    ScheduleError("script_class");
                next.scene = value != "OBJECT";
            }
            if (order != v.annotations.end()) {
                const auto value = Upper(order->second);
                next.order = value == "STANDARD"      ? 0
                             : value == "PREPROCESS"  ? 1
                             : value == "POSTPROCESS" ? 2
                                                      : -1;
                if (next.order < 0)
                    ScheduleError("script_order");
            }
            if (v.annotations.count("SCRIPT"))
                ScheduleError("global_technique_expression_not_supported");
            for (const auto& [name, value] : v.annotations)
                if (name != "SCRIPTCLASS" && name != "SCRIPTORDER" && name != "SCRIPTOUTPUT")
                    ScheduleError("global_annotation/" + name);
                else if (name == "SCRIPTOUTPUT" && Upper(value) != "COLOR")
                    ScheduleError("script_output");
        }
    for (const auto& t : p.textures) {
        if (t.semantic != "RENDERCOLORTARGET" && t.semantic != "RENDERDEPTHSTENCILTARGET" &&
            t.semantic != "OFFSCREENRENDERTARGET") {
            // Uploads are resolved from the host in P5.4, never allocated as targets.
            continue;
        }
        TargetDeclaration d;
        d.name = t.name;
        d.semantic = t.semantic;
        d.type = t.type;
        d.shared = t.shared;
        d.publisher = !t.shared || !t.annotations.empty();
        d.target.name = t.name;
        auto& target = d.target;
        target.depth = t.semantic == "RENDERDEPTHSTENCILTARGET";
        target.offscreen = t.semantic == "OFFSCREENRENDERTARGET";
        for (const auto& [key, value] : t.annotations) {
            if (key == "WIDTH" || key == "HEIGHT") {
                const auto n = Numbers(value, 1)[0];
                if (n < 1 || n > 4096 || std::floor(n) != n)
                    ScheduleError("target_extent");
                (key == "WIDTH" ? target.width : target.height) = unsigned(n);
            } else if (key == "VIEWPORTRATIO") {
                const auto n = Numbers(value, 2);
                if (n[0] <= 0 || n[1] <= 0)
                    ScheduleError("target_ratio");
                target.relative = true;
                target.viewportRatio = {n[0], n[1]};
            } else if (key == "FORMAT") {
                const auto f = Upper(value);
                if (target.depth ? f != "D24S8" : f != "A8R8G8B8" && f != "A16B16G16R16F")
                    ScheduleError("target_format");
                target.format = f == "A16B16G16R16F" ? MmePortableFormat::Rgba16Float
                                                     : MmePortableFormat::Rgba8;
            } else if (key == "MIPLEVELS" || key == "LEVELS") {
                const auto n = Numbers(value, 1)[0];
                if (n != 0 && n != 1)
                    ScheduleError("partial_mips");
                target.mipmapped = n == 0;
            } else if (key == "CLEARCOLOR") {
                const auto n = Numbers(value, 4);
                for (unsigned i = 0; i < 4; ++i)
                    d.clearColor[i] = float(int(std::clamp(n[i], 0.f, 1.f) * 255)) / 255;
            } else if (key == "CLEARDEPTH") {
                d.clearDepth = Numbers(value, 1)[0];
                if (d.clearDepth < 0 || d.clearDepth > 1)
                    ScheduleError("clear_depth");
            } else if (key == "DEFAULTEFFECT") {
                std::size_t at = 0;
                while (at < value.size()) {
                    const auto end = value.find(';', at);
                    const auto row =
                        Trim(value.substr(at, end == value.npos ? value.size() - at : end - at));
                    at = end == value.npos ? value.size() : end + 1;
                    if (row.empty())
                        continue;
                    const auto eq = row.find('=');
                    if (eq == row.npos || Trim(row.substr(0, eq)).empty())
                        ScheduleError("default_effect_syntax");
                    auto key = Trim(row.substr(0, eq)), v = Trim(row.substr(eq + 1));
                    const auto upper = Upper(v);
                    if (upper.empty() || upper == "NONE")
                        v = "none";
                    else if (upper == "HIDE")
                        v = "hide";
                    else if (upper == "MAIN_DEFAULT")
                        v = "main_default";
                    else
                        v = RelativePath(binding.path.parent_path(), v).generic_u8string();
                    if (key.size() > 512 || d.defaults.size() >= 256)
                        ScheduleError("default_effect_budget");
                    d.defaults.emplace_back(std::move(key), std::move(v));
                }
            } else if (key != "DESCRIPTION")
                ScheduleError("target_annotation/" + key);
        }
        if (target.offscreen && d.publisher &&
            (!t.annotations.count("CLEARCOLOR") || !t.annotations.count("CLEARDEPTH")))
            ScheduleError("offscreen_requires_clear");
        if (target.mipmapped &&
            (!target.offscreen || target.depth || target.format != MmePortableFormat::Rgba8))
            ScheduleError("target_mips");
        next.targets.push_back(std::move(d));
    }
    for (unsigned i = 0; i < p.techniques.size(); ++i) {
        const auto& t = p.techniques[i];
        TechniqueRule rule;
        rule.index = i;
        for (const auto& [key, value] : t.annotations) {
            if (key == "MMDPASS") {
                const auto n = Upper(value);
                rule.mode = n == "OBJECT"      ? 0
                            : n == "OBJECT_SS" ? 1
                            : n == "SHADOW"    ? 2
                            : n == "EDGE"      ? 3
                            : n == "ZPLOT"     ? 4
                                               : -1;
                if (rule.mode < 0)
                    ScheduleError("mmd_pass");
            } else if (key == "SUBSET") {
                rule.allSubsets = false;
                if (!mme_rules::SubsetRanges(rule.subsets, value))
                    ScheduleError("subset_range");
            } else if (key == "USETEXTURE" || key == "USESPHEREMAP" || key == "USETOON") {
                auto n = Upper(value);
                if (n != "TRUE" && n != "FALSE" && n != "1" && n != "0")
                    ScheduleError("use_flag");
                rule.use[key == "USETEXTURE"     ? 0
                         : key == "USESPHEREMAP" ? 1
                                                 : 2] = (n == "TRUE" || n == "1") ? 1 : 0;
            } else if (key == "SCRIPT")
                rule.script = Commands(value, p, t);
            else
                ScheduleError("technique_annotation/" + key);
        }
        if (rule.script.empty() && t.annotations.count("SCRIPT") && !t.passes.empty())
            ScheduleError("empty_script");
        if (rule.script.empty())
            for (unsigned local = 0; local < t.passes.size(); ++local)
                rule.script.push_back({MmePortableCommand::Kind::Pass, "", int(local)});
        for (auto pass : t.passes) {
            const auto& a = p.passes.at(pass).annotations;
            auto script = a.count("SCRIPT") ? Upper(a.at("SCRIPT")) : "";
            script.erase(std::remove_if(script.begin(), script.end(),
                                        [](unsigned char c) { return std::isspace(c); }),
                         script.end());
            if (next.scene ? a.size() != 1 || !a.count("SCRIPT") || script != "DRAW=BUFFER;"
                           : !a.empty())
                ScheduleError("pass_script_not_supported");
        }
        next.techniques.push_back(std::move(rule));
    }
    if (next.scene ? (next.order != 2 || next.techniques.size() != 1) : next.order != 0)
        ScheduleError("scene_profile");
    output = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
bool SelectTechnique(const ScheduleDescription& d, int mode, int subset, unsigned flags,
                     const std::vector<bool>& valid, int& selected, std::string& reason) try {
    if (valid.size() != d.techniques.size())
        ScheduleError("technique_capabilities_missing");
    int last = -1;
    selected = -1;
    if (subset < 0) {
        reason.clear();
        return true;
    }
    for (const auto& t : d.techniques) {
        if (t.mode != mode)
            continue;
        if (!t.allSubsets && std::none_of(t.subsets.begin(), t.subsets.end(), [&](const auto& r) {
                return subset >= r.first && subset <= r.second;
            }))
            continue;
        bool match = true;
        for (unsigned i = 0; i < 3; ++i)
            if (t.use[i] >= 0 && bool(t.use[i]) != bool(flags & (1u << i)))
                match = false;
        if (!match)
            continue;
        if (valid[t.index]) {
            selected = int(t.index);
            reason.clear();
            return true;
        }
        last = int(t.index);
    }
    if (last >= 0)
        ScheduleError("matched_technique_invalid");
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
}
