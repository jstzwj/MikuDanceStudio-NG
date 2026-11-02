#pragma once
#include "mikudancestudio/pmm_edit.hpp"

inline void FillBlinkCapacity(mikudancestudio::format::PmmSceneDocument& document, unsigned free) {
    using namespace mikudancestudio::format;
    auto& model = document.models[0];
    auto& track = model.morphKeys;
    const auto roots = unsigned(track.dense.size());
    const unsigned channel = roots == 2 ? 1 : 0;
    const unsigned end = 20000 - free;
    for (auto& key : track.dense)
        key.previous = key.next = 0;
    track.dense[channel].next = roots;
    track.sparse.clear();
    track.unused.clear();
    for (unsigned i = roots; i < end; ++i) {
        PmmMorphKey key;
        key.frame = 1000 + i;
        key.previous = i == roots ? channel : i - 1;
        key.next = i + 1 == end ? 0 : i + 1;
        key.value = .25f;
        track.sparse.push_back({int(i), 0, key});
    }
    model.selection.maxFrame = 1000 + end - 1;
}
