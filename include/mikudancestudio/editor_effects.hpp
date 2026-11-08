#pragma once
#include "mme_runtime.hpp"
#include <functional>
#include <tuple>
namespace mikudancestudio::editor {
struct EffectAddress {
    bool accessory = false;
    std::uint8_t slot = 255;
    int subset = -1;
    bool operator<(const EffectAddress& b) const {
        return std::tie(accessory, slot, subset) < std::tie(b.accessory, b.slot, b.subset);
    }
};
inline std::uint64_t EffectObjectId(bool accessory, std::uint8_t slot) {
    return 1 + unsigned(slot) + (accessory ? 256 : 0);
}
struct EffectAssignment {
    std::filesystem::path source;
    bool enabled = true;
    std::uint64_t reload = 1;
    std::map<std::string, mme::ParameterValue> parameters;
};
struct EffectDocument {
    std::map<EffectAddress, EffectAssignment> assignments;
    // A deliberate user choice, never an implicit successful fallback.
    bool basicMaterials = false;
};
bool ValidEffectParameter(const std::string&, const mme::ParameterValue&);
// UTF-8, bounded versioned sidecar; PMM content identity prevents accidentally
// applying assignments to another project. Paths are relative to the PMM.
std::uint64_t EffectProjectIdentity(const std::vector<std::uint8_t>&);
bool EncodeEffects(const EffectDocument&, const std::filesystem::path& project,
                   std::uint64_t identity, std::vector<std::uint8_t>&, std::string&);
bool DecodeEffects(const std::vector<std::uint8_t>&, const std::filesystem::path& project,
                   std::uint64_t identity, EffectDocument&, std::string&);
std::filesystem::path EffectSidecar(const std::filesystem::path& project);
bool LoadProjectEffects(const std::filesystem::path&, EffectDocument&, std::string&);
using EffectAtomicWrite = std::function<bool(const std::filesystem::path&,
                                             const std::vector<std::uint8_t>&, std::string&)>;
bool SaveProjectEffects(const EffectDocument&, const std::filesystem::path&,
                        const std::vector<std::uint8_t>& project, const EffectAtomicWrite&,
                        std::string&);
}
