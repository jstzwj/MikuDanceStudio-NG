#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmd_writer.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mikudancestudio::editor {
bool EditorSession::SaveEnhancedModel(std::uint8_t slot, const std::filesystem::path& path,
                                      const AtomicWrite& write, std::string& reason) {
    const auto fail = [&](const char* message) {
        reason = message;
        return false;
    };
    if (playing_)
        return fail("editor-is-playing");
    const auto resource = runtime_->Resource(slot);
    if (!resource)
        return fail("model-not-loaded");
    // The original disables the enhancement submenu for a PMX model.
    if (!resource->PmdSource())
        return fail("enhanced-model-requires-pmd");
    if (!write)
        return fail("missing-atomic-writer");
    if (path.empty())
        return fail("model-save-path-empty");
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    auto diskPath = absolute;
    if (!path_.empty()) {
        const auto relative = absolute.lexically_relative(path_.parent_path());
        if (!relative.empty())
            diskPath = relative;
    }
    std::string encoded;
    if (!text::EncodeCp932(diskPath.generic_u8string(), encoded) || encoded.size() > 255 ||
        encoded.find('\0') != std::string::npos)
        return fail("model-save-path-not-representable");
    std::replace(encoded.begin(), encoded.end(), '/', '\\');
    std::vector<char> newPath(encoded.begin(), encoded.end());
    format::PmmModelEnglishNames names;
    if (!ModelEnglishNames(slot, names, reason))
        return false;
    auto model = *resource->PmdSource();
    const auto copy = [](auto& destination, const std::string& value) {
        std::memset(&destination, 0, sizeof destination);
        std::memcpy(&destination, value.data(), std::min(value.size(), sizeof(destination) - 1));
    };
    copy(model.englishModel.name, names.name);
    copy(model.englishModel.comment, names.comment);
    model.englishBoneNames.resize(model.bones.size());
    for (unsigned i = 0; i < model.bones.size(); ++i)
        copy(model.englishBoneNames[i], names.bones[i]);
    model.englishMorphNames.resize(model.morphs.empty() ? 0 : model.morphs.size() - 1);
    for (unsigned i = 0; i < model.englishMorphNames.size(); ++i)
        copy(model.englishMorphNames[i], names.morphs[i + 1]);
    model.englishGroupNames.resize(model.groupNames.size());
    const unsigned firstGroup = model.facialTargets.empty() ? 2 : 3;
    for (unsigned i = 0; i < model.groupNames.size(); ++i)
        copy(model.englishGroupNames[i], names.groups[i + firstGroup]);
    std::array<std::string, 10> toons;
    if (!ModelToonNames(slot, toons, reason))
        return false;
    for (unsigned i = 0; i < 10; ++i)
        copy(model.toonNames[i], toons[i]);
    std::vector<std::uint8_t> bytes;
    if (!format::WriteEnhancedPmd(model, bytes, reason))
        return false;
    const bool saved = write(absolute, bytes, reason);
    // Command 263 clears the application reminder after the writer returns,
    // including an IO error, then marks the PMM modified without bone history.
    enhancedModelDirty_ = false;
    revision_ = nextRevision_++;
    if (!saved)
        return false;
    if (!runtime_->SetModelPath(slot, std::move(newPath)))
        return fail("model-not-loaded");
    state_.assets.modelPaths[slot] = absolute;
    reason.clear();
    return true;
}
}
