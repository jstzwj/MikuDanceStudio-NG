#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>

namespace mikudancestudio::editor {
std::string EnglishNameBytes(std::string_view utf8, std::size_t limit) {
    std::string result;
    for (std::size_t i = 0; i < utf8.size() && result.size() < limit;) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        if (!c)
            break;
        std::size_t length = c < 0x80                ? 1
                             : c >= 0xc2 && c < 0xe0 ? 2
                             : c >= 0xe0 && c < 0xf0 ? 3
                             : c >= 0xf0 && c < 0xf5 ? 4
                                                     : 1;
        length = std::min(length, utf8.size() - i);
        std::string encoded;
        if (!text::EncodeCp932(utf8.substr(i, length), encoded))
            encoded = "?";
        result += encoded;
        i += length;
    }
    if (result.size() > limit)
        result.resize(limit);
    return result;
}
std::string EnglishNameDisplay(std::string_view bytes) {
    std::string result;
    for (std::size_t i = 0; i < bytes.size() && bytes[i];) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        const std::size_t length = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc) ? 2 : 1;
        std::string decoded;
        if (i + length <= bytes.size() && text::DecodeCp932(bytes.substr(i, length), decoded)) {
            result += decoded;
            i += length;
        } else {
            result += "\xef\xbf\xbd";
            ++i;
        }
    }
    return result;
}
bool EditorSession::ModelEnglishNames(std::uint8_t slot, format::PmmModelEnglishNames& output,
                                      std::string& reason) const {
    const auto resource = runtime_->Resource(slot);
    const auto& models = Document().models;
    const auto model = std::find_if(models.begin(), models.end(),
                                    [slot](const auto& m) { return m.slot == slot; });
    if (!resource || model == models.end()) {
        reason = "model-not-loaded";
        return false;
    }
    format::PmmModelEnglishNames names;
    if (model->englishNames)
        names = *model->englishNames;
    else {
        const auto decode = [](const format::PmxText& bytes, std::size_t limit) {
            std::string utf8;
            text::DecodeUtf16Le(bytes.data(), bytes.size(), utf8);
            return EnglishNameBytes(utf8, limit);
        };
        const auto& doc = resource->Document();
        names.comment = decode(doc.texts.fields[3], 255);
        for (const auto& bone : doc.bones)
            names.bones.push_back(decode(bone.names[1], 19));
        for (const auto& morph : doc.morphs)
            names.morphs.push_back(decode(morph.names[1], 19));
        for (const auto& group : resource->DisplayGroups())
            names.groups.push_back(EnglishNameBytes(group.names[1], 49));
    }
    names.name.assign(model->sourceName.begin(), model->sourceName.end());
    names.name.resize(std::min({names.name.size(), names.name.find('\0'), std::size_t(49)}));
    output = std::move(names);
    return true;
}
bool EditorSession::SetEnglishName(std::uint8_t slot, EnglishNameKind kind, unsigned index,
                                   std::string_view utf8, std::string& reason) {
    format::PmmModelEnglishNames names;
    if (!ModelEnglishNames(slot, names, reason))
        return false;
    std::string* target = nullptr;
    switch (kind) {
    case EnglishNameKind::Model:
        target = &names.name;
        break;
    case EnglishNameKind::Comment:
        target = &names.comment;
        break;
    case EnglishNameKind::Bone:
        if (index < names.bones.size())
            target = &names.bones[index];
        break;
    case EnglishNameKind::Morph:
        if (index < names.morphs.size())
            target = &names.morphs[index];
        break;
    case EnglishNameKind::Group:
        if (index < names.groups.size())
            target = &names.groups[index];
        break;
    }
    if (!target) {
        reason = "english-name-index";
        return false;
    }
    auto bytes = EnglishNameBytes(utf8, kind == EnglishNameKind::Comment ? 255
                                        : kind == EnglishNameKind::Group ? 49
                                                                         : 19);
    if (kind == EnglishNameKind::Comment)
        bytes.erase(std::remove(bytes.begin(), bytes.end(), '\r'), bytes.end());
    *target = std::move(bytes);
    if (!runtime_->SetModelEnglishNames(
            slot, std::make_shared<const format::PmmModelEnglishNames>(std::move(names)))) {
        reason = "model-not-loaded";
        return false;
    }
    ++appearanceRevision_;
    return true;
}
bool EditorSession::MarkEnhancedModel(std::uint8_t slot, std::string& reason) {
    format::PmmModelEnglishNames names;
    if (!ModelEnglishNames(slot, names, reason))
        return false;
    names.enhanced = true;
    if (!runtime_->SetModelEnglishNames(
            slot, std::make_shared<const format::PmmModelEnglishNames>(std::move(names))))
        return false;
    enhancedModelDirty_ = true; // Native application flag, retained after model unload/save.
    return true;
}
}
