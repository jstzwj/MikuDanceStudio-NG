#pragma once
#include "editor_morph_scale_fixture.hpp"
inline bool MorphPurgeFixture(mikudancestudio::editor::EditorSession& session,
                              const std::filesystem::path& root, const char* file, bool reverse,
                              unsigned length, std::string& reason) {
    if (!MorphScaleFixture(session, root, file, reason))
        return false;
    if (!session.Edit(
            "morph-purge-input",
            [&](auto& d, auto&) {
                auto& model = d.models[0];
                if (reverse)
                    std::reverse(model.morphNames.begin(), model.morphNames.end());
                const auto roots = unsigned(model.morphKeys.dense.size());
                for (unsigned i = 0; i < roots; ++i) {
                    auto& head = model.morphKeys.dense[i];
                    if (!length)
                        head.next = 0;
                }
                for (auto& key : model.morphKeys.sparse)
                    if (key.index >= int(roots) && key.index < int(3 * roots) && length == 1 &&
                        (unsigned(key.index) - roots) % 2 == 0)
                        key.value.next = 0;
                model.morphKeys.sparse.erase(
                    std::remove_if(model.morphKeys.sparse.begin(), model.morphKeys.sparse.end(),
                                   [&](const auto& key) {
                                       return key.index >= int(roots) &&
                                              key.index < int(3 * roots) &&
                                              (unsigned(key.index) - roots) % 2 >= length;
                                   }),
                    model.morphKeys.sparse.end());
                model.selection.maxFrame = 777;
                d.timeline.lastRegisteredFrame = 888;
                return true;
            },
            reason))
        return false;
    std::set<mikudancestudio::editor::KeyAddress> marks;
    if (!mikudancestudio::format::ReadPmmSelectedKeys(session.Document(), 0, marks))
        return false;
    return session.SetSelectedKeys(marks, reason);
}
