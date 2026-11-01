#pragma once
#include "frame_line_deletion_fixture.hpp"
#include "mikudancestudio/pmm_bone_registration.hpp"

namespace bone_registration_fixture {
using namespace mikudancestudio::format;
struct Case {
    unsigned order, frame;
    bool automatic;
    std::set<unsigned> selected;
    unsigned Mode() const { return frame % 4; }
    bool PhysicsEnabled() const { return order != 1; }
};
template <class F> void Visit(F&& f) {
    for (unsigned order = 0; order < 3; ++order)
        for (bool automatic : {false, true})
            for (unsigned frame : {0, 1, 2, 15, 16, 30, 31})
                for (const auto& selected :
                     std::vector<std::set<unsigned>>{{0}, {1}, {2}, {0, 1, 2}})
                    f(Case{order, frame, automatic, selected});
}
inline PmmKeyTrack<PmmBoneKey> Track(unsigned order) {
    auto track = frame_line_fixture::Make<PmmBoneKey>(order);
    // Non-physics bones retain existing physics bytes, but newly allocated
    // records begin at zero. Include this distinction in the original oracle.
    track.dense[1].physicsDisabled = 1;
    for (auto& record : track.sparse)
        if (record.index == 5 || record.index == 17)
            record.value.physicsDisabled = 1;
    return track;
}
inline std::array<float, 7> Current(unsigned bone, unsigned frame) {
    return {float(frame) / 2 + float(bone + 1), -2.f * bone, float(bone + 3), 0, .6f, 0, .8f};
}
inline std::array<float, 7> Backup(unsigned bone) {
    return {float(10 + bone), float(20 + bone), float(30 + bone), 0, 0, .6f, .8f};
}
inline PmmBoneRegistrationPose Pose(const Case& c, unsigned bone) {
    const bool physics = bone != 1;
    const bool disabled = bone == 2;
    const auto values = physics && (c.Mode() == 1 || (c.Mode() >= 2 && !disabled))
                            ? Backup(bone)
                            : Current(bone, c.frame);
    PmmBoneRegistrationPose pose;
    std::copy_n(values.begin(), 3, pose.position.begin());
    std::copy_n(values.begin() + 3, 4, pose.rotation.begin());
    if (physics)
        pose.physicsDisabled = !c.PhysicsEnabled();
    return pose;
}
inline bool Register(const Case& c, PmmKeyTrack<PmmBoneKey>& track,
                     std::map<std::uint32_t, PmmBoneKey>& saved) {
    for (auto& key : track.dense)
        key.allocated = 0;
    for (auto& record : track.sparse)
        record.value.allocated = 0;
    for (auto bone : c.selected)
        if (!RegisterPmmBonePose(track, bone, c.frame, Pose(c, bone), c.automatic, saved))
            return false;
    return true;
}
}
