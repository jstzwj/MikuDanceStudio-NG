#pragma once
#include "mikudancestudio/rhi_shader.hpp"
#include "mme_legacy_api.h"
namespace mikudancestudio::mme::detail {
bool ShaderState(const MmeLegacyPass&, bool material, const rhi::ModelPipeline&,
                 rhi::BatchRasterState&, std::string&);
rhi::SamplerDesc ShaderSampler(const MmeLegacySampler&);
}
