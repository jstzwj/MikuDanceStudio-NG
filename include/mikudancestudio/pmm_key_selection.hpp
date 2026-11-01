#pragma once
#include "mikudancestudio/pmm_scene_document.hpp"
#include <set>
#include <tuple>

namespace mikudancestudio::format {
enum class PmmKeyKind {
    Bone,
    Morph,
    Display,
    Camera,
    Light,
    Shadow,
    Gravity,
    Accessory
};
struct PmmKeyAddress {
    PmmKeyKind track = PmmKeyKind::Camera;
    std::uint8_t slot = 255;
    std::uint32_t channel = 0, frame = 0;
    bool operator<(const PmmKeyAddress& b) const {
        return std::tie(track, slot, channel, frame) <
               std::tie(b.track, b.slot, b.channel, b.frame);
    }
    bool operator==(const PmmKeyAddress& b) const {
        return std::tie(track, slot, channel, frame) ==
               std::tie(b.track, b.slot, b.channel, b.frame);
    }
};
using PmmKeyPools = std::set<std::pair<PmmKeyKind, std::uint8_t>>;
inline bool PmmModelKey(PmmKeyKind kind) {
    return kind == PmmKeyKind::Bone || kind == PmmKeyKind::Morph || kind == PmmKeyKind::Display;
}
inline bool PmmActiveKey(std::uint8_t model, const PmmKeyAddress& key) {
    return model == 255 ? !PmmModelKey(key.track) : PmmModelKey(key.track) && key.slot == model;
}
struct PmmKeyRangeSelection {
    std::uint32_t first = 0, last = 0;
    // Replace these complete pools, including unused slots; append to the
    // active forward chains of these rows (whose address frame is zero).
    PmmKeyPools replace;
    std::set<PmmKeyAddress> append;
};
bool ReadPmmSelectedKeys(const PmmSceneDocument&, std::uint8_t model, std::set<PmmKeyAddress>&);
// Atomically apply changed UI addresses. Unchanged projected addresses retain
// each record's mark, including overlapping active/detached records. Explicit
// pool clears normalize all its marks, including unused slots.
bool SetPmmSelectedKeys(PmmSceneDocument&, std::uint8_t model, const std::set<PmmKeyAddress>&,
                        const PmmKeyPools& clearUnused = {});
bool SelectPmmKeyRange(PmmSceneDocument&, std::uint8_t model, const PmmKeyRangeSelection&);
}
