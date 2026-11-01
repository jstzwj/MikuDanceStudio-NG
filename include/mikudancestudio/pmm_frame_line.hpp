#pragma once
#include "mikudancestudio/pmm_edit.hpp"
#include "mikudancestudio/bone_key_interpolation.hpp"
#include <type_traits>

namespace mikudancestudio::format {
template <class T> struct PmmFrameLineDeletion {
    unsigned affected = 0;
    // First capture of each native record, at its actual mutation point. A
    // neighboring record may already have changed earlier in the index scan.
    std::map<std::uint32_t, T> captured;
};

template <class T>
bool DeletePmmFrameLine(PmmKeyTrack<T>& track, std::uint32_t frame, bool autoInterpolation,
                        PmmFrameLineDeletion<T>& result) {
    static_assert(std::is_same_v<T, PmmBoneKey> || std::is_same_v<T, PmmMorphKey> ||
                  std::is_same_v<T, PmmCameraKey> || std::is_same_v<T, PmmLightKey>);
    std::vector<std::vector<T>> chains;
    if (frame > 0x7fffffffU || !PmmTrackChains(track, chains))
        return false;
    PmmFrameLineDeletion<T> next;
    std::map<std::uint32_t, T*> records;
    for (std::size_t i = 0; i < track.dense.size(); ++i)
        records.emplace(std::uint32_t(i), &track.dense[i]);
    for (auto& record : track.sparse)
        records.emplace(std::uint32_t(record.index), &record.value);
    const auto capture = [&](std::uint32_t index) {
        if constexpr (std::is_same_v<T, PmmBoneKey>)
            next.captured.emplace(index, *records.at(index));
    };
    const auto rebuild = [&](std::uint32_t index) {
        if constexpr (std::is_same_v<T, PmmBoneKey>)
            if (autoInterpolation)
                for (unsigned lane = 0; lane < 4; ++lane)
                    keyframe_edit::RebuildBoneCurve(
                        [&](std::uint32_t i) -> T& { return *records.at(i); },
                        std::uint32_t(track.dense.size()), index, lane);
    };
    // The native loops scan record indices, not chronological frame order.
    for (const auto& [index, value] : records) {
        auto& key = *value;
        if (index < track.dense.size() || !key.frame || key.frame < frame)
            continue;
        ++next.affected;
        const auto previous = key.previous, following = key.next;
        if (key.frame == frame) {
            capture(previous);
            capture(index);
            capture(following);
            records.at(previous)->next = following;
            records.at(following)->previous = previous;
            key.frame = 0;
            if constexpr (std::is_same_v<T, PmmBoneKey>) {
                key.position = {};
                key.rotation = {0, 0, 0, 1};
                key.allocated = 0;
                rebuild(following ? following : previous);
            }
            key.previous = key.next = 0;
        } else {
            capture(index);
            if (--key.frame == 0) {
                auto& root = *records.at(previous);
                root.next = following;
                if constexpr (std::is_same_v<T, PmmBoneKey>) {
                    root.interpolation = key.interpolation;
                    root.position = key.position;
                    root.rotation = key.rotation;
                    root.allocated = key.allocated;
                    // Root physicsDisabled is deliberately retained.
                } else if constexpr (std::is_same_v<T, PmmMorphKey>) {
                    root.value = key.value;
                    root.allocated = key.allocated;
                } else if constexpr (std::is_same_v<T, PmmCameraKey>) {
                    root.distance = key.distance;
                    root.eye = key.eye;
                    root.target = key.target;
                    for (unsigned row = 0; row < 4; ++row)
                        std::copy_n(key.interpolation[row].begin(), 4,
                                    root.interpolation[row].begin());
                    root.perspective = key.perspective;
                    root.fov = key.fov;
                    // Parents, selection and the last two columns survive.
                } else {
                    root.direction = key.direction;
                    root.color = key.color;
                }
                if (following)
                    records.at(following)->previous = previous;
                rebuild(following ? following : previous);
            }
        }
    }
    // Unused native slots (frame zero after deletion/merge) are not serialized.
    track.sparse.erase(std::remove_if(track.sparse.begin(), track.sparse.end(),
                                      [](const auto& record) { return !record.value.frame; }),
                       track.sparse.end());
    result = std::move(next);
    return true;
}
}
