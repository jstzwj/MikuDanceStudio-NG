#pragma once
#include "mikudancestudio/pmm_edit.hpp"

template <class T>
void FillKeyCapacity(mikudancestudio::format::PmmKeyTrack<T>& track, unsigned free) {
    using namespace mikudancestudio::format;
    const unsigned roots = unsigned(track.dense.size());
    const unsigned end = PmmKeyCapacity<T> - free;
    for (auto& key : track.dense) {
        key.previous = key.next = 0;
        if constexpr (std::is_same_v<T, PmmBoneKey> || std::is_same_v<T, PmmMorphKey> ||
                      std::is_same_v<T, PmmDisplayKey>)
            key.allocated = 1;
        else
            key.selected = 1;
    }
    track.dense[0].next = roots < end ? roots : 0;
    track.sparse.clear();
    track.unused.clear();
    track.sparse.reserve(end - roots);
    for (unsigned i = roots; i < end; ++i) {
        auto key = track.dense[0];
        key.frame = i * 2;
        key.previous = i == roots ? 0 : i - 1;
        key.next = i + 1 == end ? 0 : i + 1;
        track.sparse.push_back({int(i), 0, key});
    }
}
