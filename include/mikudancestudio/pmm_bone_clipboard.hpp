#pragma once
#include "mikudancestudio/pmm_edit.hpp"
#include "mikudancestudio/cpu_bone_axis.hpp"
#include <string_view>

namespace mikudancestudio::format {
inline bool ConstrainPmmClipboardRotation(std::array<float, 4>& rotation, const float (&axis)[3],
                                          bool mirrored) {
    struct Ops {
        void quatToAxisAngle(const float* q, float* out, float* angle) {
            std::copy_n(q, 3, out);
            *angle = 0; // The registrar does not use the angle.
        }
        void quatNormalize(float* out, const float* q) {
            const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            for (unsigned i = 0; i < 4; ++i)
                out[i] = q[i] / length;
        }
    } ops;
    auto candidate = rotation;
    // Project the reflected rotation, then undo this reflection because the
    // storage kernel below applies it after correction, like native paste.
    if (mirrored) {
        candidate[1] = -candidate[1];
        candidate[2] = -candidate[2];
    }
    cpu_bone::ConstrainRotationAxis(candidate.data(), axis, ops);
    if (!std::all_of(candidate.begin(), candidate.end(), [](float v) { return std::isfinite(v); }))
        return false;
    if (mirrored) {
        candidate[1] = -candidate[1];
        candidate[2] = -candidate[2];
    }
    rotation = candidate;
    return true;
}

template <class NameAt>
int FindPmmClipboardBone(std::string_view copied, std::size_t count, const NameAt& nameAt,
                         bool mirrored) {
    if (mirrored) {
        for (const bool left : {true, false}) {
            const auto at = copied.find(left ? "\x8d\xb6" : "\x89\x45");
            const std::string_view opposite = left ? "\x89\x45" : "\x8d\xb6";
            if (at == std::string_view::npos)
                continue;
            for (std::size_t i = 0; i < count; ++i) {
                const auto value = nameAt(i);
                const std::string_view name = value;
                const auto hit = name.find(opposite);
                if (hit != std::string_view::npos && name.substr(hit + 2) == copied.substr(at + 2))
                    return int(i);
            }
        }
    }
    for (std::size_t i = 0; i < count; ++i)
        if (nameAt(i) == copied)
            return int(i);
    return -1;
}

// The caller resolves the destination name and constrains fixed-axis rotation.
// Clipboard registration preserves the copied interpolation; it never applies
// the pose-register command's automatic smoothing or physics checkbox.
inline bool RegisterPmmBoneClipboard(PmmKeyTrack<PmmBoneKey>& track, std::uint32_t channel,
                                     const PmmBoneKey& copied, std::uint32_t frameOffset,
                                     bool writePosition, bool mirrored,
                                     std::map<std::uint32_t, PmmBoneKey>& captured,
                                     std::uint32_t& maximumFrame) {
    std::vector<std::vector<PmmBoneKey>> chains;
    if (!PmmTrackChains(track, chains) || channel >= chains.size() || copied.frame > 0x7fffffffU ||
        frameOffset > 0x7fffffffU - copied.frame)
        return false;
    const auto frame = frameOffset + copied.frame;
    const auto& chain = chains[channel];
    const auto found = std::find_if(chain.begin(), chain.end(),
                                    [&](const auto& key) { return key.frame == frame; });
    PmmBoneKey key{};
    if (found != chain.end())
        key = *found;
    else {
        auto free = std::uint32_t(track.dense.size());
        std::set<std::uint32_t> occupied;
        for (const auto& record : track.sparse)
            occupied.insert(unsigned(record.index));
        while (occupied.count(free))
            ++free;
        if (const auto old = track.unused.find(free); old != track.unused.end())
            key = old->second;
    }
    key.frame = frame;
    key.rotation = copied.rotation;
    if (writePosition || mirrored)
        key.position = copied.position;
    if (mirrored) {
        key.position[0] = -key.position[0];
        key.rotation[1] = -key.rotation[1];
        key.rotation[2] = -key.rotation[2];
    }
    key.interpolation = copied.interpolation;
    key.physicsDisabled = copied.physicsDisabled;
    key.allocated = 1;
    auto next = track;
    auto saved = captured;
    if (!RegisterPmmKey(next, channel, key, &saved))
        return false;
    // Original C9390/C9DB0 use the absolute frame only on append. An
    // insertion/overwrite compares the raw paste offset to the old maximum.
    maximumFrame = std::max(maximumFrame, chain.back().frame < frame ? frame : frameOffset);
    track = std::move(next);
    captured = std::move(saved);
    return true;
}
}
