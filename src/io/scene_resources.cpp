#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace mikudancestudio::format {
namespace {
int Read(void* context, void* destination, std::size_t length) {
    return static_cast<int>(
        static_cast<std::istream*>(context)
            ->read(static_cast<char*>(destination), static_cast<std::streamsize>(length))
            .gcount());
}
std::string RawPath(const std::array<char, 256>& path) {
    const auto end = std::find(path.begin(), path.end(), '\0');
    return {path.begin(), end};
}
std::optional<std::filesystem::path> Cp932Path(const std::string& raw) {
    std::string decoded;
    if (!text::DecodeCp932(raw, decoded) || decoded.find_first_of("\r\n\t") != std::string::npos)
        return std::nullopt;
    std::replace(decoded.begin(), decoded.end(), '\\', '/');
    return std::filesystem::u8path(decoded);
}
}
SceneResourceResolver::SceneResourceResolver(std::filesystem::path scenePath)
    : scenePath_(std::move(scenePath)), decoder_(Cp932Path) {
}
void SceneResourceResolver::AddSearchRoot(std::filesystem::path root) {
    roots_.push_back(std::move(root));
}
void SceneResourceResolver::SetPathDecoder(DecodePath decoder) {
    decoder_ = std::move(decoder);
}
void SceneResourceResolver::ReselectModel(std::uint8_t slot, std::filesystem::path path) {
    selections_[slot] = std::move(path);
}
std::vector<std::filesystem::path>
SceneResourceResolver::Candidates(SceneResourceKind kind, const std::array<char, 256>& path,
                                  const std::filesystem::path* selection) const {
    std::vector<std::filesystem::path> candidates;
    const auto append = [&](const std::filesystem::path& candidate) {
        const auto normalized = candidate.lexically_normal();
        if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
            candidates.push_back(normalized);
    };
    if (selection) {
        append(*selection);
    } else if (decoder_) {
        const auto decoded = decoder_(RawPath(path));
        if (decoded && !decoded->empty()) {
            // MMD's 0x4089F0 relocates the UserFile suffix before probing the
            // saved path. A PMM often retains a different machine's drive or
            // installation directory; resolving only relative paths loses it.
            const auto userFile = [](const std::filesystem::path& value) {
                return std::find(value.begin(), value.end(), std::filesystem::path("UserFile"));
            };
            std::filesystem::path suffix;
            for (auto it = userFile(*decoded); it != decoded->end(); ++it)
                suffix /= *it;
            std::filesystem::path projectRoot;
            const auto sceneUserFile = userFile(scenePath_);
            if (sceneUserFile != scenePath_.end())
                for (auto it = scenePath_.begin(); it != sceneUserFile; ++it)
                    projectRoot /= *it;
            if (!suffix.empty()) {
                for (const auto& root : roots_)
                    append(root / suffix);
                if (!projectRoot.empty())
                    append(projectRoot / suffix);
                else
                    append(scenePath_.parent_path() / suffix);
            }
            // Windows drive/UNC paths are not relative paths on POSIX either.
            const auto portable = decoded->generic_u8string();
            const bool absolute = decoded->is_absolute() ||
                                  (portable.size() >= 2 && portable[1] == ':') ||
                                  portable.rfind("//", 0) == 0;
            if (absolute) {
                if (decoded->is_absolute())
                    append(*decoded);
            } else {
                append(scenePath_.parent_path() / *decoded);
                for (const auto& root : roots_)
                    append(root / *decoded);
            }
            const char* folder = kind == SceneResourceKind::Model       ? "Model"
                                 : kind == SceneResourceKind::Accessory ? "Accessory"
                                 : kind == SceneResourceKind::Wave      ? "Wave"
                                                                        : "BackGround";
            for (const auto& root : roots_)
                append(root / "UserFile" / folder / decoded->filename());
            if (!projectRoot.empty())
                append(projectRoot / "UserFile" / folder / decoded->filename());
        }
    }
    return candidates;
}
bool SceneResourceResolver::ResolveFile(SceneResourceKind kind, const std::array<char, 256>& path,
                                        std::filesystem::path& resolved) const {
    for (const auto& candidate : Candidates(kind, path, nullptr)) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            resolved = std::filesystem::absolute(candidate).lexically_normal();
            return true;
        }
    }
    return false;
}
void SceneResourceResolver::ReselectAccessory(std::uint8_t slot, std::filesystem::path path) {
    accessorySelections_[slot] = std::move(path);
}
bool SceneResourceResolver::ResolveAccessory(std::uint8_t slot, const std::array<char, 256>& path,
                                             std::filesystem::path& resolved) {
    const auto selection = accessorySelections_.find(slot);
    for (const auto& candidate :
         Candidates(SceneResourceKind::Accessory, path,
                    selection == accessorySelections_.end() ? nullptr : &selection->second)) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            resolved = std::filesystem::absolute(candidate).lexically_normal();
            return true;
        }
    }
    return false;
}
bool SceneResourceResolver::ResolveModel(std::uint8_t slot, const std::array<char, 256>& path,
                                         ModelDocument& model) {
    const auto override = selections_.find(slot);
    auto candidates = Candidates(SceneResourceKind::Model, path,
                                 override == selections_.end() ? nullptr : &override->second);
    for (const auto& candidate : candidates) {
        std::ifstream file(candidate, std::ios::binary);
        ModelDocument parsed;
        if (file && ReadModelDocument(Read, &file, parsed) && parsed.kind != ModelKind::Unknown) {
            resolved_[slot] = candidate.lexically_normal();
            model = std::move(parsed);
            return true;
        }
    }
    missing_.push_back({{slot, path}, std::move(candidates)});
    return false;
}
bool SceneResourceResolver::Resolve(void* context, std::uint8_t slot,
                                    const std::array<char, 256>& path, ModelDocument& model) {
    return static_cast<SceneResourceResolver*>(context)->ResolveModel(slot, path, model);
}
}
