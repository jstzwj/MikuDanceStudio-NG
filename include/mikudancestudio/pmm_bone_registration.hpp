#pragma once
#include "mikudancestudio/pmm_edit.hpp"
#include "mikudancestudio/bone_key_interpolation.hpp"
#include <optional>

namespace mikudancestudio::format {
struct PmmBoneRegistrationPose {
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    // Native registration only writes this byte for a physics-controlled bone.
    // Ordinary bone overwrites retain it; new records start with zero.
    std::optional<std::uint8_t> physicsDisabled;
};

// The caller supplies the current/physics backup pose selected by the host.
// Capture only the first visit to each native slot, before writing its links or
// payload. Automatic smoothing can modify neighbors outside that undo entry.
inline bool RegisterPmmBonePose(PmmKeyTrack<PmmBoneKey>& track, std::uint32_t channel,
                                std::uint32_t frame, const PmmBoneRegistrationPose& pose,
                                bool automatic, std::map<std::uint32_t, PmmBoneKey>& captured) {
    std::vector<std::vector<PmmBoneKey>> chains;
    if (frame > 0x7fffffffU || !PmmTrackChains(track, chains) || channel >= chains.size() ||
        (pose.physicsDisabled && *pose.physicsDisabled > 1))
        return false;
    PmmBoneKey key{};
    const auto& chain = chains[channel];
    const auto found = std::find_if(chain.begin(), chain.end(),
                                    [&](const auto& value) { return value.frame == frame; });
    if (found != chain.end())
        key = *found;
    else
        for (unsigned lane = 0; lane < 4; ++lane)
            keyframe_edit::ResetBoneCurve(key, lane);
    key.frame = frame;
    key.position = pose.position;
    key.rotation = pose.rotation;
    key.allocated = 1;
    if (pose.physicsDisabled)
        key.physicsDisabled = *pose.physicsDisabled;
    auto next = track;
    auto saved = captured;
    if (!RegisterPmmKey(next, channel, key, &saved) ||
        (automatic && !RebuildPmmBoneInterpolation(next, channel, frame)) ||
        !PmmTrackChains(next, chains))
        return false;
    track = std::move(next);
    captured = std::move(saved);
    return true;
}
}
