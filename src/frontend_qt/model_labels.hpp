#pragma once
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <QString>

namespace mikudancestudio::qt {
inline QString ModelTrackLabel(const editor::EditorSession& session, std::uint8_t slot,
                               unsigned index, bool morph, bool english,
                               bool documentIndex = false) {
    const auto resource = session.Runtime().Resource(slot);
    if (!resource)
        return {};
    if (documentIndex) {
        int mapped;
        std::string reason;
        if (!(morph ? session.Runtime().DocumentMorphIndex(slot, int(index), mapped, reason)
                    : session.Runtime().DocumentBoneIndex(slot, int(index), mapped, reason)))
            return {};
        index = unsigned(mapped);
    }
    if (english)
        for (const auto& model : session.Document().models)
            if (model.slot == slot && model.englishNames) {
                const auto& names = morph ? model.englishNames->morphs : model.englishNames->bones;
                if (index < names.size())
                    return QString::fromUtf8(editor::EnglishNameDisplay(names[index]).c_str());
            }
    if (english)
        if (const auto* pmd = resource->PmdSource()) {
            const char* raw = nullptr;
            if (morph && index < pmd->morphs.size())
                raw = pmd->englishPresence.hasEnglish
                          ? (index && index - 1 < pmd->englishMorphNames.size()
                                 ? pmd->englishMorphNames[index - 1].data()
                                 : "")
                          : pmd->morphs[index].header.name;
            else if (!morph && index < pmd->bones.size())
                raw = pmd->englishPresence.hasEnglish && index < pmd->englishBoneNames.size()
                          ? pmd->englishBoneNames[index].data()
                          : pmd->bones[index].pmdName;
            if (raw) {
                std::size_t count = 0;
                while (count < 19 && raw[count])
                    ++count;
                return QString::fromUtf8(editor::EnglishNameDisplay({raw, count}).c_str());
            }
        }
    const auto& names = morph ? resource->Document().morphs.at(index).names
                              : resource->Document().bones.at(index).names;
    std::string utf8;
    const auto& bytes = names[english ? 1 : 0];
    text::DecodeUtf16Le(bytes.data(), bytes.size(), utf8);
    if (english)
        utf8 = editor::EnglishNameDisplay(editor::EnglishNameBytes(utf8, 19));
    return QString::fromUtf8(utf8.c_str());
}
}
