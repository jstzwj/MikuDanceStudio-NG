#pragma once
#include "mikudancestudio/editor_session.hpp"
#include "editor_key_capacity_fixture.hpp"
#include "frame_line_deletion_fixture.hpp"

inline void HashPasteCapacity(std::uint64_t& hash,
                              const mikudancestudio::format::PmmSceneDocument& d) {
    frame_line_fixture::Hash(hash, d.cameraKeys);
    frame_line_fixture::Hash(hash, d.lightKeys);
    std::vector<std::uint8_t> bytes;
    frame_line_fixture::Bytes(bytes, d.timeline.lastRegisteredFrame);
    for (auto byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
}

inline bool GlobalPasteCapacityFixture(mikudancestudio::editor::EditorSession& session,
                                       unsigned cameraFree, unsigned lightFree, unsigned frame,
                                       std::string& reason) {
    using namespace mikudancestudio::format;
    using mikudancestudio::editor::Track;
    if (!session.SelectModel(255, reason) ||
        !session.Edit(
            "paste-source",
            [](auto& d, auto&) {
                for (unsigned i = 0; i < 3; ++i) {
                    auto camera = d.cameraKeys.dense[0];
                    auto light = d.lightKeys.dense[0];
                    camera.frame = light.frame = 10 + i;
                    camera.eye[0] = float(31 + i);
                    light.color[0] = float(i + 1) / 8;
                    if (!RegisterPmmKey(d.cameraKeys, 0, camera) ||
                        !RegisterPmmKey(d.lightKeys, 0, light))
                        return false;
                }
                return true;
            },
            reason))
        return false;
    std::set<PmmKeyAddress> keys;
    for (unsigned i = 0; i < 3; ++i) {
        keys.insert({Track::Camera, 255, 0, 10 + i});
        keys.insert({Track::Light, 255, 0, 10 + i});
    }
    return session.Copy(keys, reason) && session.Edit(
                                             "paste-destination",
                                             [&](auto& d, auto&) {
                                                 FillKeyCapacity(d.cameraKeys, cameraFree);
                                                 FillKeyCapacity(d.lightKeys, lightFree);
                                                 d.timeline.currentFrame = int(frame);
                                                 return true;
                                             },
                                             reason);
}

inline bool ModelPasteCapacityFixture(mikudancestudio::editor::EditorSession& session,
                                      const std::filesystem::path& root, unsigned morphFree,
                                      unsigned displayFree, std::string& reason) {
    using namespace mikudancestudio::format;
    using mikudancestudio::editor::Track;
    if (!session.ImportModel(root / "tint-morph.pmx", reason) ||
        !session.Edit(
            "model-paste-source",
            [](auto& d, auto&) {
                auto& model = d.models[0];
                for (unsigned i = 0; i < 3; ++i) {
                    auto morph = model.morphKeys.dense[0];
                    auto display = model.displayKeys.dense[0];
                    morph.frame = display.frame = 10 + i;
                    morph.value = float(i + 1) / 8;
                    display.visible = std::uint8_t(i % 2);
                    if (!RegisterPmmKey(model.morphKeys, 0, morph) ||
                        !RegisterPmmKey(model.displayKeys, 0, display))
                        return false;
                }
                return true;
            },
            reason))
        return false;
    std::set<PmmKeyAddress> keys;
    for (unsigned i = 0; i < 3; ++i) {
        keys.insert({Track::Morph, 0, 0, 10 + i});
        keys.insert({Track::Display, 0, 0, 10 + i});
    }
    return session.Copy(keys, reason) &&
           session.Edit(
               "model-paste-destination",
               [&](auto& d, auto&) {
                   FillKeyCapacity(d.models[0].morphKeys, morphFree);
                   FillKeyCapacity(d.models[0].displayKeys, displayFree);
                   d.timeline.currentFrame = 3;
                   return true;
               },
               reason);
}
