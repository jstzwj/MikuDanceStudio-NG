#pragma once
#include "mikudancestudio/editor_session.hpp"

inline bool ReductionFixture(mikudancestudio::editor::EditorSession& session,
                             const std::filesystem::path& root, const char* file, bool reverse,
                             unsigned variant, std::string& reason) {
    if (!session.New(reason) || !session.ImportModel(root / file, reason) ||
        !session.ImportModel(root / "triangle.pmx", reason) || !session.SelectModel(0, reason))
        return false;
    return session.Edit(
        "reduction-input",
        [&](auto& d, auto&) {
            auto& model = d.models[0];
            if (reverse) {
                std::reverse(model.boneNames.begin(), model.boneNames.end());
                std::reverse(model.bonePose.begin(), model.bonePose.end());
                std::reverse(model.boneKeys.dense.begin(), model.boneKeys.dense.end());
                for (auto& index : model.ikSources)
                    index = int(model.boneNames.size()) - 1 - index;
                model.selection.selectedBone = -1;
            }
            const auto chain = [&](auto& track, unsigned detached, unsigned unused) {
                track.sparse.clear();
                track.unused.clear();
                const unsigned roots = unsigned(track.dense.size());
                for (unsigned i = 0; i < roots; ++i) {
                    auto key = track.dense[i];
                    key.frame = key.previous = key.next = 0;
                    key.allocated = 1;
                    track.dense[i] = key;
                    if (variant != 4) {
                        const unsigned first = roots + i * 3;
                        track.dense[i].next = first;
                        for (unsigned j = 0; j < 3; ++j) {
                            key.frame = (j + 1) * 10;
                            key.previous = j ? first + j - 1 : i;
                            key.next = j == 2 ? 0 : first + j + 1;
                            track.sparse.push_back({int(first + j), 0, key});
                        }
                    }
                }
                if (roots) {
                    auto key = track.dense[0];
                    key.frame = 10;
                    key.previous = key.next = 0;
                    track.sparse.push_back({int(detached), 0, key});
                    key.frame = 0;
                    track.unused[unused] = key;
                }
            };
            chain(model.boneKeys, 9000, 99999);
            chain(model.morphKeys, 9000, 19999);
            chain(model.displayKeys, 900, 999);
            if (variant == 1)
                model.boneKeys.sparse[0].value.position[0] = .25f;
            if (variant == 2)
                model.boneKeys.sparse[1].value.rotation[3] = -1;
            if (variant == 3 && !model.morphKeys.dense.empty())
                model.morphKeys.sparse[0].value.value = .5f;
            if (variant == 5)
                model.displayKeys.sparse[0].value.visible = 0;
            if (variant == 6) {
                auto& key = model.displayKeys.sparse[0].value;
                if (key.ikEnabled.empty())
                    key.visible = 0;
                else
                    key.ikEnabled[0] ^= 1;
            }
            if (variant == 7) {
                auto& key = model.boneKeys.sparse[0].value;
                key.position[0] = -0.f;
                key.physicsDisabled ^= 1;
                key.interpolation[7] = 127;
            }
            d.timeline.currentFrame = 15;
            d.models[1].boneKeys.dense[0].allocated = 1;
            d.cameraKeys.dense[0].selected = 1;
            return true;
        },
        reason);
}
