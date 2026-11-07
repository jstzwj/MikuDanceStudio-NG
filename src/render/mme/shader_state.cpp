#include "shader_state.hpp"
#include <map>
namespace mikudancestudio::mme::detail {
using namespace rhi;
bool Raster(const MmeLegacyPass& pass, BatchRasterState& raster, std::string& reason,
            bool material = false) {
    auto states = pass.states;
    const auto fail = [&] {
        reason = "MME_legacy_render_state";
        return false;
    };
    // These host-dependent features have no implicit native approximation.
    static const std::map<std::string, std::string> fixed = {{"ALPHATESTENABLE", "FALSE"},
                                                             {"STENCILENABLE", "FALSE"},
                                                             {"FILLMODE", "SOLID"},
                                                             {"DEPTHBIAS", "0"},
                                                             {"SLOPESCALEDEPTHBIAS", "0"},
                                                             {"SRGBWRITEENABLE", "FALSE"},
                                                             {"COLORWRITEENABLE", "15"},
                                                             {"SEPARATEALPHABLENDENABLE", "FALSE"},
                                                             {"BLENDOP", "ADD"}};
    for (const auto& [key, value] : fixed) {
        if (material && !states.count(key))
            continue; // Validated against the explicit CPU draw before binding.
        if (states[key] != value)
            return fail();
        states.erase(key);
    }
    if (pass.states.count("FILLMODE"))
        raster.wireframe = false;
    for (const auto* key : {"ZENABLE", "ZWRITEENABLE", "ALPHABLENDENABLE"}) {
        const auto found = states.find(key);
        if (found == states.end())
            continue; // Inherit this draw's explicit CPU state.
        if (found->second != "TRUE" && found->second != "FALSE")
            return fail();
        (std::string(key) == "ZENABLE"        ? raster.depthTest
         : std::string(key) == "ZWRITEENABLE" ? raster.depthWrite
                                              : raster.alphaBlend) = found->second == "TRUE";
        states.erase(found);
    }
    if (states.count("CULLMODE")) {
        const auto& value = states.at("CULLMODE");
        if (value != "NONE" && value != "CW" && value != "CCW")
            return fail();
        raster.cull = value == "NONE" ? BatchCull::None
                      : value == "CW" ? BatchCull::Clockwise
                                      : BatchCull::CounterClockwise;
        states.erase("CULLMODE");
    }
    static const std::map<std::string, DepthComparison> comparisons = {
        {"NEVER", DepthComparison::Never},
        {"LESS", DepthComparison::Less},
        {"EQUAL", DepthComparison::Equal},
        {"LESSEQUAL", DepthComparison::LessEqual},
        {"GREATER", DepthComparison::Greater},
        {"NOTEQUAL", DepthComparison::NotEqual},
        {"GREATEREQUAL", DepthComparison::GreaterEqual},
        {"ALWAYS", DepthComparison::Always}};
    if (states.count("ZFUNC")) {
        if (!comparisons.count(states.at("ZFUNC")))
            return fail();
        raster.depthCompare = comparisons.at(states.at("ZFUNC"));
        states.erase("ZFUNC");
    }
    static const std::map<std::string, BlendFactor> factors = {
        {"ZERO", BlendFactor::Zero},
        {"ONE", BlendFactor::One},
        {"SRCALPHA", BlendFactor::SourceAlpha},
        {"INVSRCALPHA", BlendFactor::InvSourceAlpha},
        {"DESTALPHA", BlendFactor::DestinationAlpha},
        {"INVDESTALPHA", BlendFactor::InvDestinationAlpha}};
    for (const auto* key : {"SRCBLEND", "DESTBLEND"}) {
        if (!states.count(key))
            continue;
        if (!factors.count(states.at(key)))
            return fail();
        (std::string(key) == "SRCBLEND" ? raster.sourceBlend : raster.destinationBlend) =
            factors.at(states.at(key));
        states.erase(key);
    }
    return states.empty() || fail();
}
bool Inherited(const MmeLegacyPass& pass, const ModelPipeline& host, const BatchRasterState& raster,
               std::string& reason) {
    // The CPU geometry contract already rejects wireframe, bias and sRGB.
    // Generic shaders cannot inherit native alpha test/stencil or arbitrary
    // color writes. Separate alpha and blend operations matter only when the
    // effect leaves blending enabled.
    const auto missing = [&](const char* key) { return !pass.states.count(key); };
    if ((missing("ALPHATESTENABLE") && host.shader.alphaTest) ||
        (missing("STENCILENABLE") && host.stencil) ||
        (missing("COLORWRITEENABLE") && host.colorWriteMask != 15) ||
        (raster.alphaBlend && ((missing("SEPARATEALPHABLENDENABLE") && host.separateAlpha) ||
                               (missing("BLENDOP") && host.colorOperation != ModelBlendOp::Add)))) {
        reason = "MME_legacy_inherited_material_state";
        return false;
    }
    return true;
}
SamplerDesc ShaderSampler(const MmeLegacySampler& s) {
    // Portable trilinear is the agreed default; native anisotropy is separate.
    const auto filter = [](bool linear, bool anisotropic) {
        return linear || anisotropic ? SamplerFilter::Linear : SamplerFilter::Point;
    };
    return {filter(s.linearMin, s.anisotropicMin),
            filter(s.linearMag, s.anisotropicMag),
            filter(s.linearMip, s.anisotropicMip),
            s.clampU ? SamplerAddress::Clamp : SamplerAddress::Wrap,
            s.clampV ? SamplerAddress::Clamp : SamplerAddress::Wrap,
            1};
}
bool ShaderState(const MmeLegacyPass& pass, bool material, const ModelPipeline& host,
                 BatchRasterState& raster, std::string& reason) {
    if (!Raster(pass, raster, reason, material))
        return false;
    if (material)
        return Inherited(pass, host, raster, reason);
    if (!pass.states.count("CULLMODE") || !pass.states.count("ALPHABLENDENABLE") ||
        (raster.alphaBlend &&
         (!pass.states.count("SRCBLEND") || !pass.states.count("DESTBLEND"))) ||
        (raster.depthTest && (!pass.states.count("ZWRITEENABLE") || !pass.states.count("ZFUNC")))) {
        reason = "MME_shader_scene_inherited_state";
        return false;
    }
    return true;
}
}
