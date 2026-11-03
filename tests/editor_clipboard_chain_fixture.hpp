#pragma once
#include "mikudancestudio/editor_session.hpp"
#include "frame_line_deletion_fixture.hpp"

namespace clipboard_chain_fixture {
using namespace mikudancestudio::editor;
using namespace mikudancestudio::format;
struct Command {
    bool copy;
    std::vector<unsigned> frames;
    unsigned destination = 0;
};
inline const std::vector<Command>& Commands() {
    static const std::vector<Command> commands{
        {true, {5, 17}}, {false, {}, 20},  {false, {}, 20},    {false, {}, 8},   {true, {20, 32}},
        {false, {}, 0},  {false, {}, 12},  {true, {0, 12}},    {false, {}, 100}, {false, {}, 0},
        {true, {}},      {false, {}, 200}, {true, {100, 112}}, {false, {}, 50}};
    return commands;
}
inline std::set<KeyAddress> Selection(const Command& command, unsigned accessories) {
    std::set<KeyAddress> keys;
    for (auto frame : command.frames) {
        for (auto track : {Track::Camera, Track::Light, Track::Shadow, Track::Gravity})
            keys.insert({track, 255, 0, frame});
        for (unsigned slot = 0; slot < accessories; ++slot)
            keys.insert({Track::Accessory, std::uint8_t(slot), 0, frame});
    }
    return keys;
}
inline bool Initialize(EditorSession& session, const std::filesystem::path& root,
                       unsigned accessories, bool reversed, std::string& reason) {
    for (unsigned i = 0; i < accessories; ++i)
        if (!session.ImportAccessory(root / "bound.x", reason))
            return false;
    return session.SelectModel(255, reason) &&
           session.Edit(
               "clipboard chain fixture",
               [&](auto& d, auto&) {
                   d.physics.playbackMode = 0;
                   for (unsigned n = 0; n < 2; ++n) {
                       const unsigned i = reversed ? 1 - n : n;
                       const unsigned frame = i ? 17 : 5;
                       auto camera = d.cameraKeys.dense[0];
                       camera.frame = frame;
                       camera.eye = {float(i + 1), 8, 2};
                       camera.target = {0, float(i) / 8, .25f};
                       camera.distance = -float(30 + i);
                       camera.fov = 30 + i * 10;
                       camera.parentModel = -1;
                       camera.parentBone = 0;
                       for (unsigned row = 0; row < 4; ++row)
                           for (unsigned c = 0; c < 6; ++c)
                               camera.interpolation[row][c] = std::uint8_t(20 + row * 3 + c + i);
                       auto light = d.lightKeys.dense[0];
                       light.frame = frame;
                       light.color = {.25f, float(i + 1) / 4, .75f};
                       light.direction = {float(i), -1, 2};
                       auto shadow = d.selfShadowKeys.dense[0];
                       shadow.frame = frame;
                       shadow.mode = std::uint8_t(i + 1);
                       shadow.distance = .006f + float(i) / 10000;
                       auto gravity = d.gravityKeys.dense[0];
                       gravity.frame = frame;
                       gravity.acceleration = float(7 + i);
                       gravity.direction = {0, -1, float(i) / 4};
                       gravity.noise = 10 + i;
                       gravity.noiseEnabled = std::uint8_t(i);
                       if (!RegisterPmmKey(d.cameraKeys, 0, camera) ||
                           !RegisterPmmKey(d.lightKeys, 0, light) ||
                           !RegisterPmmKey(d.selfShadowKeys, 0, shadow) ||
                           !RegisterPmmKey(d.gravityKeys, 0, gravity))
                           return false;
                       for (unsigned slot = 0; slot < accessories; ++slot) {
                           auto key = d.accessories[slot].keyRecords.dense[0];
                           key.frame = frame;
                           key.parentModel = -1;
                           key.parentBone = 0;
                           key.position = {float(i + slot), float(2 + slot), -float(i)};
                           key.rotation = {0, float(i) / 8, float(slot) / 4};
                           key.scale = .5f + float(slot + i);
                           key.opacity = float(i + 1) / 2;
                           key.visible = 1;
                           key.shadowEnabled = std::uint8_t(i);
                           if (!RegisterPmmKey(d.accessories[slot].keyRecords, 0, key))
                               return false;
                       }
                   }
                   return true;
               },
               reason);
}
inline bool Execute(EditorSession& session, const Command& command, unsigned accessories,
                    std::string& reason) {
    if (command.copy) {
        const auto keys = Selection(command, accessories);
        return session.SetSelectedKeys(keys, reason) && session.Copy(keys, reason);
    }
    return session.Seek(command.destination, reason) &&
           session.Paste(command.destination, false, reason);
}
inline void Hash(std::uint64_t& hash, const PmmSceneDocument& d) {
    frame_line_fixture::Hash(hash, d.cameraKeys);
    frame_line_fixture::Hash(hash, d.lightKeys);
    frame_line_fixture::Hash(hash, d.selfShadowKeys);
    frame_line_fixture::Hash(hash, d.gravityKeys);
    for (const auto& a : d.accessories)
        frame_line_fixture::Hash(hash, a.keyRecords);
    std::vector<std::uint8_t> bytes;
    frame_line_fixture::Bytes(bytes, d.timeline.lastRegisteredFrame);
    for (auto byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
}
} // namespace clipboard_chain_fixture
