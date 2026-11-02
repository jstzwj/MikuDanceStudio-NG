#pragma once
#include "morph_key_registration.hpp"
#include "pmm_edit.hpp"
#include <algorithm>
#include <cstring>

namespace mikudancestudio::format {
// The editable sparse representation retains every native slot, including free
// payloads. Materialize once per command, rather than searching maps per key.
class PmmMorphRegistration {
    std::vector<PmmMorphKey> records_;

public:
    bool Load(const PmmKeyTrack<PmmMorphKey>& track) {
        std::vector<std::vector<PmmMorphKey>> chains;
        if (track.dense.size() > keyframe_edit::MorphKeyCapacity || !PmmTrackChains(track, chains))
            return false;
        records_.assign(keyframe_edit::MorphKeyCapacity, {});
        std::copy(track.dense.begin(), track.dense.end(), records_.begin());
        for (const auto& key : track.sparse) {
            if (unsigned(key.index) >= records_.size())
                return false;
            records_[unsigned(key.index)] = key.value;
        }
        for (const auto& [index, key] : track.unused) {
            if (index >= records_.size())
                return false;
            records_[index] = key;
        }
        return true;
    }
    bool Register(unsigned roots, unsigned channel, std::uint32_t frame, float value,
                  std::uint32_t& maximum) {
        return keyframe_edit::RegisterCurrentMorph(
            roots, channel, frame, value, maximum,
            [&](unsigned index) -> auto& { return records_[index]; });
    }
    void Store(PmmKeyTrack<PmmMorphKey>& track) const {
        std::copy_n(records_.begin(), track.dense.size(), track.dense.begin());
        track.sparse.clear();
        track.unused.clear();
        const float zero = 0;
        for (unsigned i = unsigned(track.dense.size()); i < records_.size(); ++i) {
            const auto& key = records_[i];
            if (key.frame)
                track.sparse.push_back({int(i), 0, key});
            else if (key.previous || key.next || key.allocated ||
                     std::memcmp(&key.value, &zero, sizeof zero))
                track.unused.emplace(i, key);
        }
    }
};
}
