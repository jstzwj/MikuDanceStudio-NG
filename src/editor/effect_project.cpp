#include "mikudancestudio/editor_effects.hpp"
#include <fstream>
namespace mikudancestudio::editor {
namespace {
bool Read(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < 0 || std::uint64_t(size) > limit)
        return false;
    bytes.resize(std::size_t(size));
    file.seekg(0);
    return bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())));
}
}
bool LoadProjectEffects(const std::filesystem::path& project, EffectDocument& output,
                        std::string& reason) {
    auto sidecar = EffectSidecar(project);
    if (!std::filesystem::exists(sidecar)) {
        output = {};
        reason.clear();
        return true;
    }
    std::vector<std::uint8_t> pmm, bytes;
    if (!Read(project, pmm, 512 * 1024 * 1024) || !Read(sidecar, bytes, 16 * 1024 * 1024)) {
        reason = "effect-project-read-failed";
        return false;
    }
    const auto identity = EffectProjectIdentity(pmm);
    if (DecodeEffects(bytes, project, identity, output, reason))
        return true;
    auto backup = sidecar;
    backup += ".previous";
    if (Read(backup, bytes, 16 * 1024 * 1024) &&
        DecodeEffects(bytes, project, identity, output, reason))
        return true;
    reason = "effect-project-pair-inconsistent:" + reason;
    return false;
}
bool SaveProjectEffects(const EffectDocument& doc, const std::filesystem::path& path,
                        const std::vector<std::uint8_t>& project, const EffectAtomicWrite& write,
                        std::string& reason) {
    const auto sidecar = EffectSidecar(path);
    if (doc.assignments.empty() && !doc.basicMaterials && !std::filesystem::exists(sidecar))
        return write(path, project, reason);
    std::vector<std::uint8_t> next;
    if (!EncodeEffects(doc, path, EffectProjectIdentity(project), next, reason))
        return false;
    // Keep a recoverable sidecar for the current on-disk PMM before publishing
    // its successor. An interruption at any write leaves a matching pair.
    if (std::filesystem::exists(path)) {
        EffectDocument previous;
        std::vector<std::uint8_t> pmm, backup;
        if (!LoadProjectEffects(path, previous, reason) || !Read(path, pmm, 512 * 1024 * 1024) ||
            !EncodeEffects(previous, path, EffectProjectIdentity(pmm), backup, reason))
            return false;
        auto previousPath = sidecar;
        previousPath += ".previous";
        if (!write(previousPath, backup, reason))
            return false;
        // When only effects changed the PMM identity stays the same. Publish
        // just the atomic sidecar so a reported PMM failure cannot change state.
        if (pmm == project)
            return write(sidecar, next, reason);
    }
    if (!write(sidecar, next, reason))
        return false;
    return write(path, project, reason);
}
}
