#include "rhi_shader_spirv.hpp"
namespace mikudancestudio::rhi {
ShaderResult ValidateSpirvProgram(const ShaderProgramDesc& desc) {
    std::string reason;
    std::map<unsigned, std::vector<unsigned>> outputs, inputs;
    if (!detail::ReflectSpirv(desc, desc.vertex, desc.vertex.spirv, reason, &outputs) ||
        !detail::ReflectSpirv(desc, desc.pixel, desc.pixel.spirv, reason, &inputs))
        return {false, reason};
    for (const auto& [location, type] : inputs) {
        const auto found = outputs.find(location);
        if (found == outputs.end() || found->second != type)
            return {false, "shader_stage_interface_mismatch"};
    }
    return {true, {}};
}
}
