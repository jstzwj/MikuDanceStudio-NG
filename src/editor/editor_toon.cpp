#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace mikudancestudio::editor {
namespace {
std::string Name(const std::array<char, 100>& name) {
    return {name.data(), std::size_t(std::find(name.begin(), name.end(), 0) - name.begin())};
}
bool Shared(const std::string& name, unsigned slot) {
    char expected[16];
    std::snprintf(expected, sizeof expected, "toon%02u.bmp", slot + 1);
    return name == expected;
}
std::filesystem::path ToonPath(const std::filesystem::path& directory, const std::string& encoded) {
    std::string decoded;
    if (!text::DecodeCp932(encoded, decoded))
        return {};
    std::replace(decoded.begin(), decoded.end(), '\\', '/');
    // The native collector concatenates modelDirectory and input, rather than
    // permitting an absolute name to replace the model directory.
    return std::filesystem::u8path(directory.generic_u8string() + '/' + decoded).lexically_normal();
}
std::shared_ptr<const rhi::RgbaImage> Read(const std::filesystem::path& path) {
    if (path.empty())
        return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    if (size <= 0 || size > 16 * 1024 * 1024)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    auto image = std::make_shared<rhi::RgbaImage>();
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size) ||
        !rhi::DecodeTextureRgba(bytes.data(), bytes.size(), *image))
        return {};
    image->pixels.Seal();
    return image;
}
}
void EditorSession::InitializeModelToons(EditorAssets& assets, bool refresh) {
    for (const auto& [slot, resource] : assets.models) {
        if (!resource->PmdSource() || (!refresh && assets.modelToons.count(slot)))
            continue;
        auto entry = std::make_shared<ModelToonEdit>();
        const auto previous = assets.modelToons.find(slot);
        entry->names = previous == assets.modelToons.end() ? resource->PmdToonNames()
                                                           : previous->second->names;
        if (previous == assets.modelToons.end() &&
            !resource->PmdSource()->englishPresence.hasToonFields)
            for (unsigned i = 0; i < 10; ++i)
                std::snprintf(entry->names[i].data(), entry->names[i].size(), "toon%02u.bmp",
                              i + 1);
        const auto& paths = assets.modelTexturePaths;
        const auto found = paths.find(slot);
        const auto directory =
            (found != paths.end() ? found->second : assets.modelPaths.at(slot)).parent_path();
        for (unsigned i = 0; i < 10; ++i) {
            const auto name = Name(entry->names[i]);
            if (Shared(name, i))
                continue;
            const auto path = ToonPath(directory, name);
            entry->paths[i] = path;
            if (const auto cached = toonImages_.find(path); cached != toonImages_.end())
                entry->images[i] = cached->second;
            else if (auto image = Read(path))
                entry->images[i] = toonImages_[path] = std::move(image);
        }
        assets.modelToons[slot] = std::move(entry);
    }
}
void EditorSession::RefreshModelToons() {
    toonImages_.clear();
    InitializeModelToons(state_.assets, true);
    ++appearanceRevision_;
}
bool EditorSession::ModelToonNames(std::uint8_t slot, std::array<std::string, 10>& names,
                                   std::string& reason) const {
    const auto resource = runtime_->Resource(slot);
    if (!resource || !resource->PmdSource()) {
        reason = resource ? "toon-requires-pmd" : "model-not-loaded";
        return false;
    }
    const auto found = state_.assets.modelToons.find(slot);
    const auto& raw =
        found == state_.assets.modelToons.end() ? resource->PmdToonNames() : found->second->names;
    for (unsigned i = 0; i < 10; ++i)
        names[i] = Name(raw[i]);
    reason.clear();
    return true;
}
bool EditorSession::SetModelToonNames(std::uint8_t slot, const std::array<std::string, 10>& text,
                                      std::vector<unsigned>& missing, std::string& reason) {
    missing.clear();
    if (playing_) {
        reason = "editor-is-playing";
        return false;
    }
    std::array<std::string, 10> initial;
    if (!ModelToonNames(slot, initial, reason))
        return false;
    InitializeModelToons(state_.assets);
    auto next = std::make_shared<ModelToonEdit>(*state_.assets.modelToons.at(slot));
    const auto& assets = state_.assets;
    const auto origin = assets.modelTexturePaths.find(slot);
    const auto directory =
        (origin == assets.modelTexturePaths.end() ? assets.modelPaths.at(slot) : origin->second)
            .parent_path();
    bool changed = false;
    for (unsigned i = 0; i < 10; ++i) {
        const auto name = EnglishNameBytes(text[i], 255);
        char expected[16];
        std::snprintf(expected, sizeof expected, "toon%02u.bmp", i + 1);
        if (name == expected)
            continue; // Native default input skips BOTH probing and storing.
        const auto path = ToonPath(directory, name);
        std::shared_ptr<const rhi::RgbaImage> image;
        if (name.size() < 100 && !path.empty()) {
            const auto cached = toonImages_.find(path);
            image = cached != toonImages_.end() ? cached->second : Read(path);
        }
        if (!image) {
            missing.push_back(i);
            continue;
        }
        toonImages_[path] = image;
        next->names[i].fill(0);
        std::memcpy(next->names[i].data(), name.data(), name.size());
        next->paths[i] = path;
        next->images[i] = std::move(image);
        changed = true;
    }
    if (changed) {
        state_.assets.modelToons[slot] = std::move(next);
        ++appearanceRevision_;
    }
    if (!missing.empty()) {
        reason = "toon-texture-missing";
        return false;
    }
    // Command 261 marks only the enhanced-model reminder, with no PMM/history edit.
    enhancedModelDirty_ = true;
    reason.clear();
    return true;
}
}
