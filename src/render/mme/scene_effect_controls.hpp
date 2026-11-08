#pragma once
#include "mikudancestudio/scene_effects.hpp"
#include "mikudancestudio/cpu_math.hpp"
#include <set>
#include <stdexcept>
namespace mikudancestudio::render {
inline std::vector<mme::ControlObject> SceneControlObjects(const runtime::SceneRuntime& scene,
                                                           const SceneRenderSources& sources,
                                                           const std::set<std::string>& items) {
    std::vector<mme::ControlObject> result;
    const auto& doc = scene.Session()->Document();
    for (const auto& m : doc.models) {
        const auto resource = scene.Resource(m.slot);
        const auto* snapshot = scene.Snapshot(m.slot);
        if (!resource || !snapshot)
            continue;
        mme::ControlObject object;
        object.id = editor::EffectObjectId(false, m.slot);
        object.name = sources.models.at(m.slot).filename().u8string();
        object.order = m.previousDisplayOrder;
        object.world = cpu_math::Identity();
        for (const auto& pose : scene.Session()->Pose().models)
            if (pose.slot == m.slot)
                object.visible = pose.visible != 0;
        for (unsigned n = 0; n < snapshot->bones.size(); ++n) {
            const auto& name = resource->BoneNames().at(n);
            if (!items.count(name) && !items.count("(AttachedBone)"))
                continue;
            mme::ControlItem item;
            item.matrix = true;
            std::string reason;
            if (!scene.BoneAttachment(m.slot, int(n), item.transform, reason))
                throw std::runtime_error(reason);
            object.items.emplace(name, item);
        }
        for (unsigned n = 0; n < snapshot->morphWeights.size(); ++n) {
            if (!items.count(resource->MorphNames().at(n)))
                continue;
            mme::ControlItem item;
            item.value[0] = snapshot->morphWeights[n];
            object.items.emplace(resource->MorphNames().at(n), item);
        }
        result.push_back(std::move(object));
    }
    for (const auto& a : doc.accessories)
        if (scene.Accessory(a.slot)) {
            mme::ControlObject object;
            object.id = editor::EffectObjectId(true, a.slot);
            object.name = sources.accessories.at(a.slot).filename().u8string();
            object.order = a.order;
            object.accessory = true;
            object.visible = a.state.visible;
            const format::PmmEvaluatedAccessory* evaluated = nullptr;
            for (const auto& pose : scene.Session()->Pose().accessories)
                if (pose.slot == a.slot) {
                    evaluated = &pose;
                    object.visible = pose.visible != 0;
                }
            if (!evaluated)
                throw std::runtime_error("effect-accessory-pose-missing");
            const auto& pose = *evaluated;
            const auto scalar = [&](const char* name, float value) {
                if (items.count(name)) {
                    mme::ControlItem item;
                    item.value[0] = value;
                    object.items.emplace(name, item);
                }
            };
            for (unsigned n = 0; n < 3; ++n) {
                scalar(std::array<const char*, 3>{"X", "Y", "Z"}[n], pose.position[n]);
                scalar(std::array<const char*, 3>{"Rx", "Ry", "Rz"}[n], pose.rotation[n]);
            }
            for (const auto& entry :
                 {std::make_pair("XYZ", pose.position), std::make_pair("Rxyz", pose.rotation)})
                if (items.count(entry.first)) {
                    mme::ControlItem item;
                    item.value = entry.second;
                    object.items.emplace(entry.first, item);
                }
            // Match the original ExpGetAcsSi panel scale (10x), not a
            // decomposition of the inherited attachment/world matrix.
            scalar("Si", pose.scale * 10);
            scalar("Tr", pose.opacity);
            std::string reason;
            if (!scene.AccessoryPlacement(a.slot, object.world, reason))
                throw std::runtime_error(reason);
            if (pose.parentModel >= 0 && pose.parentModel < 255) {
                const auto model = scene.Resource(std::uint8_t(pose.parentModel));
                if (model) {
                    object.attachedModel =
                        editor::EffectObjectId(false, std::uint8_t(pose.parentModel));
                    int index = -1;
                    if (scene.DocumentBoneIndex(pose.parentModel, pose.parentBone, index, reason) &&
                        index >= 0)
                        object.attachedBone = model->BoneNames().at(unsigned(index));
                }
            }
            result.push_back(std::move(object));
        }
    return result;
}
}
