#pragma once
#include "editor_clipboard_chain_fixture.hpp"
#include <cmath>

namespace model_clipboard_chain_fixture {
using namespace mikudancestudio::editor;
using namespace mikudancestudio::format;
inline bool Initialize(EditorSession& session, const std::filesystem::path& root, const char* file,
                       bool reversed, std::string& reason) {
    return session.ImportModel(root / file, reason) &&
           session.Edit(
               "model clipboard chain fixture",
               [&](auto& d, auto&) {
                   d.physics.playbackMode = 0;
                   auto& model = d.models[0];
                   for (unsigned n = 0; n < 2; ++n) {
                       const unsigned variant = reversed ? 1 - n : n;
                       const unsigned frame = variant ? 17 : 5;
                       for (unsigned channel = 0; channel < model.boneKeys.dense.size();
                            ++channel) {
                           auto key = model.boneKeys.dense[channel];
                           key.frame = frame;
                           key.position = {};
                           const float y = float(variant + 1) / 8;
                           key.rotation = {0, y, 0, std::sqrt(1 - y * y)};
                           key.physicsDisabled = std::uint8_t(variant);
                           if (!RegisterPmmKey(model.boneKeys, channel, key))
                               return false;
                       }
                       for (unsigned channel = 0; channel < model.morphKeys.dense.size();
                            ++channel) {
                           auto key = model.morphKeys.dense[channel];
                           key.frame = frame;
                           key.value = float((channel + variant) % 4 + 1) / 4;
                           if (!RegisterPmmKey(model.morphKeys, channel, key))
                               return false;
                       }
                       auto key = model.displayKeys.dense[0];
                       key.frame = frame;
                       key.visible = std::uint8_t(variant);
                       for (auto& enabled : key.ikEnabled)
                           enabled = std::uint8_t(variant);
                       if (!RegisterPmmKey(model.displayKeys, 0, key))
                           return false;
                   }
                   return true;
               },
               reason);
}
inline std::set<KeyAddress> Selection(const EditorSession& session,
                                      const clipboard_chain_fixture::Command& command) {
    std::set<KeyAddress> result;
    for (const auto& key : session.Keys())
        if (key.slot == 0 && PmmModelKey(key.track) &&
            std::find(command.frames.begin(), command.frames.end(), key.frame) !=
                command.frames.end())
            result.insert(key);
    return result;
}
inline bool Execute(EditorSession& session, const clipboard_chain_fixture::Command& command,
                    std::string& reason) {
    if (command.copy) {
        const auto keys = Selection(session, command);
        return session.SetSelectedKeys(keys, reason) && session.Copy(keys, reason);
    }
    return session.Seek(command.destination, reason) &&
           session.Paste(command.destination, false, reason);
}
inline void Hash(std::uint64_t& hash, const PmmSceneModel& model) {
    frame_line_fixture::Hash(hash, model.boneKeys);
    frame_line_fixture::Hash(hash, model.morphKeys);
    frame_line_fixture::Hash(hash, model.displayKeys);
    std::vector<std::uint8_t> bytes;
    frame_line_fixture::Bytes(bytes, model.selection.maxFrame);
    for (auto byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
}
} // namespace model_clipboard_chain_fixture
