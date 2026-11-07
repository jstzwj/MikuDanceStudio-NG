#include "mme_legacy_api.h"
#include <stdexcept>

// Implemented beside the parser to reuse its intrinsic lowering and reachability.
bool MmeLegacyLower(MmeLegacyProgram&, const std::string& constants,
                    const std::string& declarations, std::string& reason);

bool MmeLegacyGenerate(const MmeLegacyProgram& definition, MmeLegacyProgram& modern,
                       std::vector<MmeLegacyConstant>& layout, unsigned& bytes,
                       std::string& reason) try {
    auto next = definition;
    std::vector<MmeLegacyConstant> packed;
    std::string constants, declarations;
    unsigned cursor = 0;
    for (const auto& [name, fields] : definition.structures) {
        declarations += "struct " + name + "{";
        for (const auto& f : fields)
            declarations += f.type + " " + f.name +
                            (f.semantic.empty() ? "" : ":" + f.semantic) + ";";
        declarations += "};\n";
    }
    for (unsigned i = 0; i < definition.variables.size(); ++i) {
        const auto& v = definition.variables[i];
        if (v.semantic == "STANDARDSGLOBAL" || v.type == "string")
            continue;
        std::string declaration = (v.rows == 4 ? "row_major " : "") +
                                  (v.type == "bool" ? std::string("uint") : v.type) + " " + v.name;
        if (v.array)
            declaration += "[" + std::to_string(v.elements) + "]";
        if (v.immutable) {
            if (!v.constant || v.initializer.empty())
                throw std::runtime_error("MME_shader_static_initializer");
            declarations += "static const " + declaration + "=" + v.initializer + ";\n";
            continue;
        }
        if (!v.elements || (v.rows != 1 && v.rows != 4) || !v.columns || v.columns > 4 ||
            (v.rows == 4 && (v.type != "float4x4" || v.columns != 4)) ||
            (v.rows == 1 && v.type != "float" && v.type != "float2" && v.type != "float3" &&
             v.type != "float4" && v.type != "int" && v.type != "bool"))
            throw std::runtime_error("MME_shader_parameter_shape");
        const unsigned size = v.rows * v.columns * 4;
        if (v.array || v.rows == 4 || cursor % 16 + size > 16)
            cursor = (cursor + 15) & ~15u;
        const unsigned stride = v.rows == 4 ? 64 : v.array ? 16 : size;
        if (v.elements > 256 || cursor + stride * v.elements > 65536)
            throw std::runtime_error("MME_shader_constant_budget");
        packed.push_back({i, cursor, stride});
        declaration += ":packoffset(c" + std::to_string(cursor / 16);
        if (cursor % 16)
            declaration += std::string(".") + "xyzw"[cursor % 16 / 4];
        constants += declaration + ");\n";
        cursor += stride * v.elements;
    }
    if (definition.samplers.size() > 16)
        throw std::runtime_error("MME_shader_sampler_budget");
    if (!MmeLegacyLower(next, constants, declarations, reason))
        return false;
    modern = std::move(next);
    layout = std::move(packed);
    bytes = (cursor + 15) & ~15u;
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
