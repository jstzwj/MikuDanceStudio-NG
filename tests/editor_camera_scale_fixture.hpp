#pragma once
#include "mikudancestudio/editor_session.hpp"
inline constexpr std::array<float, 16> CameraScaleIdentity{1, 0, 1, 0, 1, 0, 1, 0,
                                                           1, 0, 1, 0, 1, 0, 1, 0};
inline bool CameraScaleFixture(mikudancestudio::editor::EditorSession& session,
                               const std::filesystem::path& root, const char* file,
                               std::string& reason) {
    if (!session.New(reason) || !session.ImportModel(root / file, reason) ||
        !session.SetBone(0, 0, {9, 8, 7}, {0, 0, 0, 1}, reason) ||
        !session.SetBone(0, 0, {6, 5, 4}, {0, 0, 0, 1}, reason) || !session.Undo(reason) ||
        !session.SelectModel(255, reason))
        return false;
    if (!session.Edit(
            "camera-scale-input",
            [](auto& d, auto&) {
                auto& track = d.cameraKeys;
                track.sparse.clear();
                track.unused.clear();
                auto& head = track.dense[0];
                head.eye = {1.25f, -2.5f, -0.f};
                head.target = {.25f, -.125f, -0.f};
                head.distance = -45.f;
                head.fov = 60;
                head.selected = 1;
                head.next = 1;
                auto key = head;
                key.frame = 10;
                key.next = 2;
                key.fov = 30;
                key.eye[0] = 3;
                track.sparse.push_back({1, 0, key});
                key.frame = 20;
                key.previous = 1;
                key.next = 0;
                key.selected = 0;
                key.fov = 90;
                track.sparse.push_back({2, 0, key});
                // Same visible address as the selected key, but a distinct native mark.
                key.frame = 10;
                key.previous = 0;
                track.sparse.push_back({100, 0, key});
                key.frame = 0;
                key.fov = std::uint32_t(-30);
                track.unused[9998] = key;
                key.selected = 1;
                track.unused[9999] = key;
                d.timeline.currentFrame = 15;
                d.cameraState[3] = 11;
                d.cameraState[4] = -12;
                d.lightState[0] = .875f;
                d.lightKeys.dense[0].selected = 1;
                d.models[0].boneKeys.dense[0].allocated = 1;
                return true;
            },
            reason))
        return false;
    std::set<mikudancestudio::editor::KeyAddress> marks;
    return mikudancestudio::format::ReadPmmSelectedKeys(session.Document(), 255, marks) &&
           session.SetSelectedKeys(marks, reason);
}
