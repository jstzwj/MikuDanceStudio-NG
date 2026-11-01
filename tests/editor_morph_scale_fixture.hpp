#pragma once
#include "mikudancestudio/editor_session.hpp"
inline bool MorphScaleFixture(mikudancestudio::editor::EditorSession& session,
                              const std::filesystem::path& root, const char* file,
                              std::string& reason) {
    if (!session.New(reason) || !session.ImportModel(root / file, reason) ||
        !session.ImportModel(root / "triangle.pmx", reason) || !session.SelectModel(0, reason))
        return false;
    if (!session.Edit(
            "morph-scale-input",
            [](auto& d, auto&) {
                auto& track = d.models[0].morphKeys;
                track.sparse.clear();
                track.unused.clear();
                const auto roots = unsigned(track.dense.size());
                for (unsigned i = 0; i < roots; ++i) {
                    auto& head = track.dense[i];
                    const auto first = roots + 2 * i;
                    head.value = .25f * (i + 1);
                    head.allocated = i % 2;
                    head.next = first;
                    auto key = head;
                    key.frame = 10;
                    key.previous = i;
                    key.next = first + 1;
                    key.value = .75f;
                    key.allocated = 1;
                    track.sparse.push_back({int(first), 0, key});
                    key.frame = 20;
                    key.previous = first;
                    key.next = 0;
                    key.value = -.125f;
                    key.allocated = 0;
                    track.sparse.push_back({int(first + 1), 0, key});
                    key.frame = 10;
                    key.previous = i;
                    key.allocated = 0;
                    track.sparse.push_back({int(100 + i), 0, key});
                }
                if (roots) {
                    auto key = track.dense[0];
                    key.previous = key.next = 0;
                    key.value = .625f;
                    key.allocated = 1;
                    track.unused[19999] = key;
                    key.value = -.75f;
                    key.allocated = 0;
                    track.unused[19998] = key;
                }
                d.timeline.currentFrame = 15;
                d.models[1].boneKeys.dense[0].allocated = 1;
                d.cameraKeys.dense[0].selected = 1;
                return true;
            },
            reason))
        return false;
    std::set<mikudancestudio::editor::KeyAddress> marks;
    if (!mikudancestudio::format::ReadPmmSelectedKeys(session.Document(), 0, marks))
        return false;
    return session.SetSelectedKeys(marks, reason);
}
