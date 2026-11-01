#pragma once
#include "mikudancestudio/pmm_frame_line.hpp"

namespace mikudancestudio::format {
struct PmmKeyDeletionOptions {
    bool autoInterpolation = true;
    std::uint8_t shadowMode = 1;
    std::set<std::uint32_t> physicsBones;
};

// Native marked-key deletion scans slots, including roots, in index order.
// A selected successor can reconnect a light/shadow/gravity head after its
// reset cut; processing all roots last would lose that behavior.
template <class T>
bool DeletePmmKeyRecords(PmmKeyTrack<T>& track, const std::set<std::uint32_t>& selected,
                         const PmmKeyDeletionOptions& options, PmmFrameLineDeletion<T>& result) {
    std::vector<std::vector<T>> checked;
    if (!PmmTrackChains(track, checked) || options.shadowMode > 1)
        return false;
    std::map<std::uint32_t, T*> records;
    for (std::uint32_t i = 0; i < track.dense.size(); ++i)
        records.emplace(i, &track.dense[i]);
    for (auto& key : track.sparse)
        records.emplace(unsigned(key.index), &key.value);
    for (auto index : selected)
        if (!records.count(index))
            return false;
    PmmFrameLineDeletion<T> next;
    // Root zero's unused predecessor can point at a freed native slot. Such a
    // capture still participates in undo, but has no serialized key payload.
    auto holes = track.unused;
    const auto at = [&](std::uint32_t index) -> T& {
        const auto found = records.find(index);
        return found == records.end() ? holes[index] : *found->second;
    };
    const auto capture = [&](std::uint32_t index) {
        if constexpr (std::is_same_v<T, PmmBoneKey>)
            next.captured.emplace(index, at(index));
    };
    for (auto index : selected) {
        auto& key = at(index);
        const bool root = index < track.dense.size();
        const auto previous = key.previous, following = key.next;
        ++next.affected;
        capture(index);
        capture(previous);
        capture(following);
        if (!root) {
            at(previous).next = following;
            at(following).previous = previous;
            key.frame = 0;
        }
        if constexpr (std::is_same_v<T, PmmBoneKey>) {
            key.position = {};
            key.rotation = {0, 0, 0, 1};
            key.allocated = 0;
            if (options.autoInterpolation)
                for (unsigned lane = 0; lane < 4; ++lane)
                    keyframe_edit::RebuildBoneCurve(at, std::uint32_t(track.dense.size()),
                                                    following ? following : previous, lane);
            if (!root)
                key.next = 0;
            key.previous = 0;
            if (root && options.physicsBones.count(index))
                key.physicsDisabled = 0;
        } else if constexpr (std::is_same_v<T, PmmMorphKey>) {
            key.value = 0;
            key.allocated = 0;
            key.previous = 0;
            if (!root)
                key.next = 0;
        } else if constexpr (std::is_same_v<T, PmmDisplayKey>) {
            key.visible = 1;
            std::fill(key.ikEnabled.begin(), key.ikEnabled.end(), 1);
            std::fill(key.rigidStates.begin(), key.rigidStates.end(), PmmRigidState{-1, 0});
            key.allocated = 0;
            key.previous = 0;
            if (!root)
                key.next = 0;
        } else {
            if (!root)
                key.previous = key.next = 0;
            key.selected = 0;
            if constexpr (std::is_same_v<T, PmmCameraKey>) {
                key.distance = -45;
                key.eye = {0, root ? 10.f : 0.f, 0};
                key.target = {};
                key.parentModel = -1;
                key.parentBone = 0;
                for (unsigned row = 0; row < 4; ++row)
                    key.interpolation[row].fill(row < 2 ? 20 : 107);
            } else if constexpr (std::is_same_v<T, PmmAccessoryKey>) {
                key.parentModel = -1;
                key.parentBone = 0;
                key.position = key.rotation = {};
                key.opacity = key.scale = 1;
                key.visible = 1;
                key.shadowEnabled = 0;
            } else {
                if (root)
                    key.next = 0;
                if constexpr (std::is_same_v<T, PmmLightKey>) {
                    key.color =
                        root ? std::array<float, 3>{.602f, .602f, .602f} : std::array<float, 3>{};
                    key.direction =
                        root ? std::array<float, 3>{-.5f, -1, .5f} : std::array<float, 3>{};
                } else if constexpr (std::is_same_v<T, PmmSelfShadowKey>) {
                    key.mode = options.shadowMode;
                    key.distance = .01125f;
                } else {
                    static_assert(std::is_same_v<T, PmmGravityKey>);
                    key.noiseEnabled = 0;
                    key.noise = 10;
                    key.acceleration = 9.8000002f;
                    key.direction = {0, -1, 0};
                }
            }
        }
    }
    track.unused = std::move(holes);
    for (const auto& key : track.sparse)
        if (!key.value.frame)
            track.unused[unsigned(key.index)] = key.value;
    track.sparse.erase(std::remove_if(track.sparse.begin(), track.sparse.end(),
                                      [](const auto& key) { return !key.value.frame; }),
                       track.sparse.end());
    result = std::move(next);
    return true;
}
}
