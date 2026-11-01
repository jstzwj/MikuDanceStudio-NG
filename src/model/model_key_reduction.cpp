#include "mikudancestudio/model.hpp"
#include "mikudancestudio/key_reduction.hpp"

namespace mikudancestudio::mdl {
// Menu 222: mark against original neighbours; deletion runs afterwards.
std::int32_t MarkRedundantModelKeys(ModelRecord& model) {
    for (std::size_t i = 0; i < kBoneKeyCapacity; ++i)
        model.boneKeys[i].allocated = 0;
    for (std::size_t i = 0; i < kMorphKeyCapacity; ++i)
        model.morphKeys[i].allocated = 0;
    for (std::size_t i = 0; i < kDisplayKeyCapacity; ++i)
        model.displayKeys[i].allocated = 0;
    unsigned count = 0;
    const auto reduce = [&](unsigned root, auto* keys, auto&& same) {
        count += keyframe_edit::MarkRedundantInterior(
            root, [&](unsigned i) -> auto& { return keys[i]; }, same,
            [&](unsigned i) { keys[i].allocated = 1; });
    };
    for (unsigned bone = 0; bone < model.boneCount; ++bone)
        if (model.boneTable[bone].type != BoneType::InertTip &&
            model.boneTable[bone].type != BoneType::Effector)
            reduce(bone, model.boneKeys,
                   [](const auto& a, const auto& b) { return keyframe_edit::SameBonePose(a, b); });
    for (unsigned morph = 0; morph < model.morphCount; ++morph)
        reduce(morph, model.morphKeys,
               [](const auto& a, const auto& b) { return a.value == b.value; });
    reduce(0, model.displayKeys, [&](const auto& a, const auto& b) {
        if (a.visible != b.visible)
            return false;
        for (unsigned i = 0; i < model.ikChainCount; ++i)
            if (a.ikStates[i] != b.ikStates[i])
                return false;
        for (unsigned i = 0; i < model.boneOrderCount; ++i)
            if (a.selectorStates[i].modelIndex != b.selectorStates[i].modelIndex ||
                a.selectorStates[i].boneIndex != b.selectorStates[i].boneIndex)
                return false;
        return true;
    });
    return std::int32_t(count);
}
}
