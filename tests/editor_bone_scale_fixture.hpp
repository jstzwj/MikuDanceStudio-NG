#pragma once
#include "mikudancestudio/editor_session.hpp"
inline constexpr std::array<float, 12> BoneScaleIdentity{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
inline bool BoneScaleFixture(mikudancestudio::editor::EditorSession& session,
                             const std::filesystem::path& root, const char* file,
                             const mikudancestudio::cpu_math::Quaternion& rotation,
                             std::string& reason) {
    if (!session.New(reason) || !session.ImportModel(root / file, reason) ||
        !session.ImportModel(root / "triangle.pmx", reason) || !session.SelectModel(0, reason))
        return false;
    if (!session.Edit(
            "bone-scale-input",
            [&](auto& d, auto&) {
                auto& model = d.models[0];
                auto& track = model.boneKeys;
                track.sparse.clear();
                track.unused.clear();
                for (auto& head : track.dense) {
                    head.position = {1.25f, -2.5f, -0.f};
                    head.rotation = rotation;
                    head.allocated = 0;
                }
                auto& head = track.dense[0];
                const auto first = unsigned(track.dense.size());
                head.next = first;
                head.allocated = 1;
                auto key = head;
                key.frame = 10;
                key.next = first + 1;
                key.position[0] = 3;
                track.sparse.push_back({int(first), 0, key});
                key.frame = 20;
                key.previous = first;
                key.next = 0;
                key.allocated = 0;
                key.physicsDisabled = 1;
                track.sparse.push_back({int(first + 1), 0, key});
                key.frame = 10;
                key.previous = 0;
                track.sparse.push_back({100, 0, key});
                key.frame = 0;
                track.unused[99998] = key;
                key.allocated = 1;
                track.unused[99999] = key;
                d.timeline.currentFrame = 15;
                model.morphKeys.dense[0].allocated = 1;
                d.models[1].boneKeys.dense[0].allocated = 1;
                d.cameraKeys.dense[0].selected = 1;
                return true;
            },
            reason))
        return false;
    std::set<mikudancestudio::editor::KeyAddress> marks;
    return mikudancestudio::format::ReadPmmSelectedKeys(session.Document(), 0, marks) &&
           session.SetSelectedKeys(marks, reason);
}
