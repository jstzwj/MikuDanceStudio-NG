#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vmd_scene_tracks.hpp"
#include <cstring>

namespace mikudancestudio::editor {
using namespace format;
namespace {
PmmSceneModel* Find(PmmSceneDocument& d, std::uint8_t slot) {
    for (auto& m : d.models)
        if (m.slot == slot)
            return &m;
    return nullptr;
}
template <std::size_t N> bool Name(const std::vector<char>& bytes, std::array<char, N>& out) {
    if (bytes.size() > N)
        return false;
    out.fill(0);
    std::copy(bytes.begin(), bytes.end(), out.begin());
    return true;
}
template <class T> std::vector<T> All(const PmmKeyTrack<T>& track) {
    std::vector<T> result = track.dense;
    for (const auto& k : track.sparse)
        result.push_back(k.value);
    return result;
}
}
bool EditorSession::ImportMotion(const VmdDocument& motion, std::uint8_t slot,
                                 std::string& reason) {
    std::shared_ptr<const runtime::MotionClip> clip;
    if (slot != 255) {
        auto resource = state_.assets.models.find(slot);
        if (resource == state_.assets.models.end()) {
            reason = "motion-model-unavailable";
            return false;
        }
        if (!runtime::MotionClip::Create(resource->second, motion, clip, reason))
            return false;
    }
    return Edit(
        "import-motion",
        [&](auto& d, auto& error) {
            if (slot == 255) {
                if (!motion.bones.empty() || !motion.morphs.empty() || !motion.ik.empty()) {
                    error = "model-motion-needs-selection";
                    return false;
                }
                for (const auto& source : motion.cameras) {
                    PmmCameraKey k;
                    if (!DecodeVmdSceneKey(source, k) || !RegisterPmmKey(d.cameraKeys, 0, k))
                        return false;
                }
                for (const auto& source : motion.lights) {
                    PmmLightKey k;
                    if (!DecodeVmdSceneKey(source, k) || !RegisterPmmKey(d.lightKeys, 0, k))
                        return false;
                }
                for (const auto& source : motion.shadows) {
                    PmmSelfShadowKey k;
                    if (!DecodeVmdSceneKey(source, k) || !RegisterPmmKey(d.selfShadowKeys, 0, k))
                        return false;
                }
                return true;
            }
            auto* m = Find(d, slot);
            if (!m)
                return false;
            // MotionClip validates source names/curves/rotations using the same
            // importer as playback. Only explicitly present tracks are merged;
            // synthetic frame-zero defaults must not erase existing animation.
            std::set<std::string> boneNames, morphNames;
            const auto names = [&](const auto& keys, auto& result) {
                for (const auto& key : keys) {
                    auto end = std::find(key.name.begin(), key.name.end(), 0);
                    std::string name;
                    if (!text::DecodeCp932({key.name.data(), std::size_t(end - key.name.begin())},
                                           name))
                        return false;
                    result.insert(name);
                }
                return true;
            };
            if (!names(motion.bones, boneNames) || !names(motion.morphs, morphNames))
                return false;
            const auto& resource = *state_.assets.models.at(slot);
            for (std::size_t i = 0; i < clip->BoneTracks().size(); ++i)
                if (boneNames.count(resource.BoneNames()[i])) {
                    auto encoded = std::vector<char>{};
                    std::string name;
                    text::EncodeCp932(resource.BoneNames()[i], name);
                    encoded.assign(name.begin(), name.end());
                    auto found = std::find(m->boneNames.begin(), m->boneNames.end(), encoded);
                    if (found == m->boneNames.end())
                        return false;
                    for (auto k : clip->BoneTracks()[i]) {
                        // Ignore generated root when input has no explicit frame zero.
                        if (!k.frame &&
                            std::none_of(
                                motion.bones.begin(), motion.bones.end(), [&](const auto& s) {
                                    return s.frame == 0 &&
                                           std::string(s.name.data(),
                                                       std::size_t(std::find(s.name.begin(),
                                                                             s.name.end(), 0) -
                                                                   s.name.begin())) == name;
                                }))
                            continue;
                        k.allocated = 1;
                        if (!RegisterPmmKey(m->boneKeys, std::size_t(found - m->boneNames.begin()),
                                            k))
                            return false;
                    }
                }
            for (std::size_t i = 0; i < clip->MorphTracks().size(); ++i)
                if (morphNames.count(resource.MorphNames()[i])) {
                    std::string name;
                    text::EncodeCp932(resource.MorphNames()[i], name);
                    auto found = std::find(m->morphNames.begin(), m->morphNames.end(),
                                           std::vector<char>(name.begin(), name.end()));
                    if (found == m->morphNames.end())
                        return false;
                    for (const auto& source : clip->MorphTracks()[i]) {
                        if (!source.frame &&
                            std::none_of(
                                motion.morphs.begin(), motion.morphs.end(), [&](const auto& s) {
                                    return s.frame == 0 &&
                                           std::string(s.name.data(),
                                                       std::size_t(std::find(s.name.begin(),
                                                                             s.name.end(), 0) -
                                                                   s.name.begin())) == name;
                                }))
                            continue;
                        PmmMorphKey k;
                        k.frame = source.frame;
                        k.value = source.value;
                        k.allocated = 1;
                        if (!RegisterPmmKey(m->morphKeys,
                                            std::size_t(found - m->morphNames.begin()), k))
                            return false;
                    }
                }
            if (!motion.ik.empty())
                for (auto k : clip->DisplayTrack()) {
                    if (!k.frame && std::none_of(motion.ik.begin(), motion.ik.end(),
                                                 [](const auto& s) { return s.frame == 0; }))
                        continue;
                    k.rigidStates = m->displayKeys.dense[0].rigidStates;
                    k.allocated = 1;
                    if (!RegisterPmmKey(m->displayKeys, 0, k))
                        return false;
                }
            return true;
        },
        reason);
}
bool EditorSession::ImportPose(const VpdDocument& pose, std::uint8_t slot, std::string& reason) {
    auto resource = state_.assets.models.find(slot);
    if (resource == state_.assets.models.end()) {
        reason = "pose-model-unavailable";
        return false;
    }
    std::shared_ptr<const runtime::MotionClip> validated;
    if (!runtime::MotionClip::Create(resource->second, pose, validated, reason))
        return false;
    return Edit(
        "import-pose",
        [&](auto& d, auto&) {
            auto* m = Find(d, slot);
            if (!m)
                return false;
            for (const auto& source : pose.bones) {
                std::string name;
                text::DecodeCp932(source.name, name);
                const auto& names = resource->second->BoneNames();
                auto found = std::find(names.begin(), names.end(), name);
                auto disk = std::find(m->boneNames.begin(), m->boneNames.end(),
                                      std::vector<char>(source.name.begin(), source.name.end()));
                if (found == names.end() || disk == m->boneNames.end())
                    continue;
                const auto& key = validated->BoneTracks()[std::size_t(found - names.begin())][0];
                auto& target = m->bonePose[std::size_t(disk - m->boneNames.begin())];
                std::copy(key.position.begin(), key.position.end(), target.components.begin());
                std::copy(key.rotation.begin(), key.rotation.end(), target.components.begin() + 3);
            }
            return true;
        },
        reason);
}
bool EditorSession::ExportMotion(std::uint8_t slot, VmdDocument& output,
                                 std::string& reason) const {
    VmdDocument result;
    auto d = Document();
    if (slot == 255) {
        for (const auto& k : All(d.cameraKeys)) {
            VmdCameraKey out;
            out.frame = k.frame;
            out.distance = k.distance;
            out.fov = k.fov;
            out.perspective = k.perspective;
            std::copy(k.eye.begin(), k.eye.end(), out.eye);
            std::copy(k.target.begin(), k.target.end(), out.target);
            for (int c = 0; c < 6; ++c)
                for (int p = 0; p < 4; ++p)
                    out.interpolation[c][p] = k.interpolation[p == 1 ? 2 : p == 2 ? 1 : p][c];
            result.cameras.push_back(out);
        }
        for (const auto& k : All(d.lightKeys)) {
            VmdLightKey out;
            out.frame = k.frame;
            std::copy(k.color.begin(), k.color.end(), out.color);
            std::copy(k.direction.begin(), k.direction.end(), out.direction);
            result.lights.push_back(out);
        }
        for (const auto& k : All(d.selfShadowKeys))
            result.shadows.push_back({k.frame, k.mode, k.distance});
    } else {
        auto* m = Find(d, slot);
        if (!m) {
            reason = "motion-model-unavailable";
            return false;
        }
        if (!Name(m->name, result.modelName)) {
            reason = "vmd-model-name-too-long";
            return false;
        }
        std::vector<std::vector<PmmBoneKey>> bones;
        std::vector<std::vector<PmmMorphKey>> morphs;
        if (!PmmTrackChains(m->boneKeys, bones) || !PmmTrackChains(m->morphKeys, morphs))
            return false;
        for (std::size_t i = 0; i < bones.size(); ++i)
            for (const auto& k : bones[i]) {
                VmdBoneKey out;
                if (!Name(m->boneNames[i], out.name)) {
                    reason = "vmd-bone-name-too-long";
                    return false;
                }
                out.frame = k.frame;
                std::copy(k.position.begin(), k.position.end(), out.position);
                std::copy(k.rotation.begin(), k.rotation.end(), out.rotation);
                for (int c = 0; c < 4; ++c)
                    for (int p = 0; p < 4; ++p)
                        for (int duplicate = 0; duplicate < 4; ++duplicate)
                            out.interpolation[c * 16 + p * 4 + duplicate] =
                                k.interpolation[p * 4 + c];
                if (k.physicsDisabled) {
                    out.interpolation[2] = 0x63;
                    out.interpolation[3] = 0x0f;
                }
                result.bones.push_back(out);
            }
        for (std::size_t i = 0; i < morphs.size(); ++i)
            for (const auto& k : morphs[i]) {
                VmdMorphKey out;
                if (!Name(m->morphNames[i], out.name)) {
                    reason = "vmd-morph-name-too-long";
                    return false;
                }
                out.frame = k.frame;
                out.value = k.value;
                result.morphs.push_back(out);
            }
        for (const auto& k : All(m->displayKeys)) {
            VmdIkKey out;
            out.frame = k.frame;
            out.visible = k.visible;
            for (std::size_t i = 0; i < m->ikSources.size(); ++i) {
                VmdIkEntry entry;
                if (!Name(m->boneNames.at(std::size_t(m->ikSources[i])), entry.name)) {
                    reason = "vmd-ik-name-too-long";
                    return false;
                }
                entry.enabled = k.ikEnabled[i];
                out.entries.push_back(entry);
            }
            result.ik.push_back(std::move(out));
        }
    }
    output = std::move(result);
    reason.clear();
    return true;
}
bool EditorSession::ExportPose(std::uint8_t slot, VpdDocument& output, std::string& reason) const {
    auto d = Document();
    auto* m = Find(d, slot);
    if (!m) {
        reason = "pose-model-unavailable";
        return false;
    }
    VpdDocument result;
    result.modelName.assign(m->name.begin(), m->name.end());
    for (std::size_t i = 0; i < m->bonePose.size(); ++i) {
        VpdBone out;
        out.name.assign(m->boneNames[i].begin(), m->boneNames[i].end());
        std::copy_n(m->bonePose[i].components.begin(), 3, out.translation);
        std::copy_n(m->bonePose[i].components.begin() + 3, 4, out.rotation);
        result.bones.push_back(out);
    }
    output = std::move(result);
    reason.clear();
    return true;
}
bool EditorSession::UnloadModel(std::uint8_t slot, std::string& reason) {
    auto next = Current();
    if (!next.assets.models.erase(slot)) {
        reason = "model-selection-expired";
        return false;
    }
    next.assets.modelPaths.erase(slot);
    auto& d = next.document;
    d.models.erase(std::remove_if(d.models.begin(), d.models.end(),
                                  [&](const auto& m) { return m.slot == slot; }),
                   d.models.end());
    d.modelRegistrations.clear();
    const auto clear = [&](auto& ref) {
        if (ref.parentModel == slot)
            ref.parentModel = ref.parentBone = -1;
    };
    for (auto& k : d.cameraKeys.dense)
        clear(k);
    for (auto& k : d.cameraKeys.sparse)
        clear(k.value);
    for (auto& a : d.accessories) {
        clear(a.state);
        for (auto& k : a.keyRecords.dense)
            clear(k);
        for (auto& k : a.keyRecords.sparse)
            clear(k.value);
    }
    if (d.appearance.cameraParentModel == slot)
        d.appearance.cameraParentModel = d.appearance.cameraParentBone = -1;
    for (auto& m : d.models) {
        const auto key = [&](auto& k) {
            for (auto& p : k.rigidStates)
                if (p.modelIndex == slot)
                    p.modelIndex = p.boneIndex = -1;
        };
        for (auto& k : m.displayKeys.dense)
            key(k);
        for (auto& k : m.displayKeys.sparse)
            key(k.value);
        for (auto& p : m.rigidPose)
            if (p.values[0] == slot)
                p.values[0] = p.values[1] = -1;
    }
    next.selection = {};
    d.envelope.selectedModelSlot = d.models.empty() ? 0 : d.models.front().slot;
    return Commit(std::move(next), reason);
}
bool EditorSession::UnloadAccessory(std::uint8_t slot, std::string& reason) {
    auto next = Current();
    if (!next.assets.accessories.erase(slot)) {
        reason = "accessory-selection-expired";
        return false;
    }
    next.assets.accessoryPaths.erase(slot);
    auto& d = next.document;
    for (std::size_t i = 0; i < d.accessories.size(); ++i)
        if (d.accessories[i].slot == slot) {
            d.accessories.erase(d.accessories.begin() + i);
            d.accessoryListNames.erase(d.accessoryListNames.begin() + i);
            break;
        }
    next.selection.accessory = 255;
    next.selection.keys.clear();
    return Commit(std::move(next), reason);
}
}
