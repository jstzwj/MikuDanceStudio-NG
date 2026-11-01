#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/cpu_deform.hpp"
#include "mikudancestudio/vmd_bone_interpolation.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/cpu_morph.hpp"
#include "mikudancestudio/cpu_bone_transform.hpp"
#include "mikudancestudio/cpu_bone_axis.hpp"
#include "runtime_model_import.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace mikudancestudio::runtime {
namespace {
template <class Range> bool Finite(const Range& values) {
    for (float v : values)
        if (!std::isfinite(v))
            return false;
    return true;
}
bool Fail(std::string& reason, const char* code, std::size_t index = 0) {
    reason = std::string(code) + ":" + std::to_string(index);
    return false;
}
bool Name(const format::PmxText& bytes, std::string& output) {
    return text::DecodeUtf16Le(bytes.data(), bytes.size(), output) && !output.empty() &&
           output.find('\0') == std::string::npos;
}
template <std::size_t N> bool MotionName(const std::array<char, N>& bytes, std::string& output) {
    return Cp932Identifier(bytes.data(), bytes.size(), output);
}
void ConstrainRotation(float* rotation, const format::BoneData& bone) {
    if (bone.flags & cpu_bone::kBoneFlagFixedAxis) {
        struct Ops {
            void quatToAxisAngle(const float* q, float* axis, float* angle) {
                std::copy_n(q, 3, axis);
                *angle = 2 * std::acos(q[3]);
            }
            void quatNormalize(float* out, const float* q) {
                const float length =
                    std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                for (int c = 0; c < 4; ++c)
                    out[c] = q[c] / length;
            }
        } ops;
        float axis[3];
        cpu_model::Math math;
        math.normalize(axis, bone.axis);
        cpu_bone::ConstrainRotationAxis(rotation, axis, ops);
    }
}
template <class Key> void SortTrack(std::vector<Key>& keys) {
    std::stable_sort(keys.begin(), keys.end(),
                     [](const Key& a, const Key& b) { return a.frame < b.frame; });
    // Registrar semantics: the last input key for a frame replaces the old one.
    std::vector<Key> result;
    for (const auto& key : keys) {
        if (!result.empty() && result.back().frame == key.frame)
            result.back() = key;
        else
            result.push_back(key);
    }
    keys = std::move(result);
}
template <class Key>
std::pair<const Key*, const Key*> Bracket(const std::vector<Key>& keys, unsigned frame) {
    const auto right = std::lower_bound(keys.begin(), keys.end(), frame,
                                        [](const Key& key, unsigned f) { return key.frame < f; });
    if (right == keys.end())
        return {&keys.back(), &keys.back()};
    if (right == keys.begin() || right->frame == frame)
        return {&*right, &*right};
    return {&*(right - 1), &*right};
}
}

bool ModelResource::Create(const format::ModelDocument& source,
                           std::shared_ptr<const ModelResource>& output, std::string& reason) {
    if (source.kind != format::ModelKind::Pmx && source.kind != format::ModelKind::Pmd)
        return Fail(reason, "unsupported-model-format");
    format::PmxDocument normalized;
    if (source.kind == format::ModelKind::Pmd && !NormalizePmd(source.pmd, normalized, reason))
        return false;
    const auto& model = source.kind == format::ModelKind::Pmd ? normalized : source.pmx;
    if (model.header.version != 2 || model.header.textEncoding != 0)
        return Fail(reason, "unsupported-pmx-version-or-encoding");
    if (model.header.additionalUvCount > 4)
        return Fail(reason, "unsupported-additional-uv");
    if (!model.rigids.empty() && model.bones.empty())
        return Fail(reason, "missing-physics-bones");
    for (std::size_t n = 0; n < model.rigids.size(); ++n) {
        const auto& r = model.rigids[n].fields;
        if (r.boneIndex < -1 || r.boneIndex >= std::int64_t(model.bones.size()) || r.group > 15 ||
            r.mode > 2 || r.shape > 2 || !Finite(r.size) || r.size[0] <= 0 ||
            (r.shape == 1 && (r.size[1] <= 0 || r.size[2] <= 0)) ||
            (r.shape == 2 && r.size[1] < 0) || !Finite(r.position) || !Finite(r.rotation) ||
            !std::isfinite(r.mass) || r.mass < 0 || !std::isfinite(r.linearDamping) ||
            !std::isfinite(r.angularDamping) || !std::isfinite(r.restitution) ||
            !std::isfinite(r.friction))
            return Fail(reason, "invalid-rigid", n);
    }
    for (std::size_t n = 0; n < model.joints.size(); ++n) {
        const auto& j = model.joints[n].fields;
        if (j.rigidA < 0 || j.rigidB < 0 || j.rigidA >= std::int64_t(model.rigids.size()) ||
            j.rigidB >= std::int64_t(model.rigids.size()) || j.rigidA == j.rigidB ||
            !Finite(j.position) || !Finite(j.rotation) || !Finite(j.limits) || !Finite(j.springs))
            return Fail(reason, "invalid-joint", n);
    }
    auto resource = std::shared_ptr<ModelResource>(new ModelResource);
    std::map<std::string, std::size_t> names;
    std::vector<std::vector<std::size_t>> children(model.bones.size());
    for (std::size_t n = 0; n < model.bones.size(); ++n) {
        const auto& b = model.bones[n].fields;
        if (!Finite(b.position))
            return Fail(reason, "nonfinite-bone", n);
        if (b.parent < -1 || b.parent >= static_cast<std::int64_t>(model.bones.size()) ||
            b.parent == static_cast<std::int64_t>(n))
            return Fail(reason, "invalid-bone-parent", n);
        if (b.parent >= 0)
            children[b.parent].push_back(n);
        else
            resource->order_.push_back(n);
        std::string name;
        if (!Name(model.bones[n].names[0], name))
            return Fail(reason, "invalid-bone-name", n);
        if (!names.emplace(name, n).second)
            return Fail(reason, "ambiguous-bone-name", n);
        resource->boneNames_.push_back(std::move(name));
    }
    for (std::size_t n = 0; n < resource->order_.size(); ++n)
        for (auto child : children[resource->order_[n]])
            resource->order_.push_back(child);
    if (resource->order_.size() != model.bones.size())
        return Fail(reason, "cyclic-bone-hierarchy");
    bool alreadyOrdered = true;
    for (std::size_t n = 0; n < model.bones.size(); ++n)
        if (model.bones[n].fields.parent >= std::int64_t(n))
            alreadyOrdered = false;
    if (alreadyOrdered)
        for (std::size_t n = 0; n < resource->order_.size(); ++n)
            resource->order_[n] = n;

    for (std::size_t n = 0; n < model.vertices.size(); ++n) {
        const auto& v = model.vertices[n];
        if (v.weightType > 3)
            return Fail(reason, "unsupported-skinning", n);
        for (unsigned set = 0; set < model.header.additionalUvCount; ++set)
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(v.additionalUv[c][set]))
                    return Fail(reason, "nonfinite-additional-uv", n);
        if (!Finite(v.position) || !Finite(v.normal) || !Finite(v.uv))
            return Fail(reason, "nonfinite-vertex", n);
        const int count = v.weightType == 0 ? 1 : v.weightType == 2 ? 4 : 2;
        for (int b = 0; b < count; ++b)
            if (v.bone[b] < -1 || v.bone[b] >= static_cast<std::int64_t>(model.bones.size()))
                return Fail(reason, "invalid-skin-bone", n);
        if (v.weightType) {
            const int weights = v.weightType == 2 ? 4 : 1;
            float sum = 0;
            for (int b = 0; b < weights; ++b) {
                if (!std::isfinite(v.weight[b]) || v.weight[b] < 0 || v.weight[b] > 1)
                    return Fail(reason, "invalid-skin-weight", n);
                sum += v.weight[b];
            }
            if (weights == 4 && std::abs(sum - 1) > 1e-4f)
                return Fail(reason, "invalid-skin-weight-sum", n);
        }
    }
    for (auto index : model.indices)
        if (index < 0 || index >= static_cast<std::int64_t>(model.vertices.size()))
            return Fail(reason, "invalid-vertex-index");
    names.clear();
    for (std::size_t n = 0; n < model.morphs.size(); ++n) {
        const auto& morph = model.morphs[n];
        if (morph.header.type >= 4 && morph.header.type <= 7 &&
            morph.header.type - 3 > model.header.additionalUvCount)
            return Fail(reason, "undeclared-uv-morph-channel", n);
        if (morph.header.type > 8)
            return Fail(reason, "unsupported-morph", n);
        std::string name;
        if (!Name(morph.names[0], name))
            return Fail(reason, "invalid-morph-name", n);
        names.emplace(name, n); // Native VMD registration binds the first duplicate name.
        resource->morphNames_.push_back(std::move(name));
        for (const auto& offset : morph.offsets) {
            const unsigned type = morph.header.type;
            const std::size_t limit = type == 0   ? model.morphs.size()
                                      : type == 2 ? model.bones.size()
                                      : type == 8 ? model.materials.size()
                                                  : model.vertices.size();
            if ((offset.index < 0 && !(type == 8 && offset.index == -1)) ||
                (offset.index >= 0 && std::size_t(offset.index) >= limit))
                return Fail(reason, "invalid-morph-offset", n);
            const int values = type == 0 ? 1 : type == 1 ? 3 : type == 2 ? 7 : type == 8 ? 28 : 4;
            for (int c = 0; c < values; ++c)
                if (!std::isfinite(offset.values[c]))
                    return Fail(reason, "invalid-morph-offset", n);
            if (type == 8 && offset.operation > 1)
                return Fail(reason, "invalid-material-morph-operation", n);
        }
    }
    if (!BuildSkeleton(model, source.kind, source.pmd.ikChains, resource->order_,
                       resource->boneNames_, resource->skeleton_, resource->chains_, reason))
        return false;
    resource->sourceKind_ = source.kind;
    bool operationCenter = false;
    for (unsigned i = 0; i < resource->boneNames_.size(); ++i) {
        if (resource->boneNames_[i] == u8"\u64cd\u4f5c\u4e2d\u5fc3") {
            resource->cameraCenter_ = i;
            operationCenter = true;
        } else if (!operationCenter && resource->boneNames_[i] == u8"\u30bb\u30f3\u30bf\u30fc")
            resource->cameraCenter_ = i;
    }
    for (const auto& bone : resource->skeleton_) {
        resource->layers_.push_back(bone.layer);
        const auto& source = model.bones[resource->editingBones_.size()].fields;
        BoneEditingInfo edit{bone.type, bone.flags, bone.hasRigidBody, bone.tailBone};
        for (unsigned c = 0; c < 3; ++c)
            edit.tailPosition[c] = source.position[c] + source.tailOffset[c];
        resource->editingBones_.push_back(edit);
    }
    for (const auto& rigid : model.rigids)
        if (rigid.fields.mode > 0 && rigid.fields.boneIndex >= 0)
            resource->editingBones_[std::size_t(rigid.fields.boneIndex)].hasRigidBody = true;
    std::sort(resource->layers_.begin(), resource->layers_.end());
    resource->layers_.erase(std::unique(resource->layers_.begin(), resource->layers_.end()),
                            resource->layers_.end());
    auto& groups = resource->displayGroups_;
    const auto decode = [](const format::PmxText& bytes) {
        std::string value;
        text::DecodeUtf16Le(bytes.data(), bytes.size(), value);
        return value;
    };
    if (!model.bones.empty()) {
        if (source.kind == format::ModelKind::Pmx && !model.frames.empty() &&
            !model.frames[0].entries.empty() && model.frames[0].entries[0].type == 0) {
            const auto root = model.frames[0].entries[0].index;
            if (root < 0 || std::size_t(root) >= model.bones.size())
                return Fail(reason, "invalid-display-root");
            resource->displayRoot_ = unsigned(root);
        }
        groups.push_back({0,
                          {resource->boneNames_[resource->displayRoot_], {}},
                          {{false, resource->displayRoot_}}});
        groups.push_back({1, {}, {}});
        std::vector<ModelDisplayEntry> facial;
        if (source.kind == format::ModelKind::Pmd) {
            for (auto index : source.pmd.facialTargets) {
                if (index >= model.morphs.size())
                    return Fail(reason, "invalid-display-morph");
                facial.push_back({true, index});
            }
        } else {
            for (const auto& frame : model.frames)
                for (const auto& entry : frame.entries) {
                    if (entry.type > 1 || entry.index < 0 ||
                        std::size_t(entry.index) >=
                            (entry.type ? model.morphs.size() : model.bones.size()))
                        return Fail(reason, "invalid-display-entry");
                    if (entry.type)
                        facial.push_back({true, unsigned(entry.index)});
                }
            facial.resize(std::uint8_t(facial.size())); // native byte-sized facial row count
        }
        const bool hasFacial = !facial.empty();
        if (hasFacial)
            groups.push_back({2, {}, std::move(facial)});
        if (source.kind == format::ModelKind::Pmd) {
            for (std::size_t i = 0; i < source.pmd.groupNames.size(); ++i) {
                ModelDisplayGroup group;
                const auto& name = source.pmd.groupNames[i];
                Cp932Identifier(name.data(), name.size(), group.names[0]);
                if (i < source.pmd.englishGroupNames.size()) {
                    const auto& en = source.pmd.englishGroupNames[i];
                    Cp932Identifier(en.data(), en.size(), group.names[1]);
                }
                groups.push_back(std::move(group));
            }
            for (const auto& entry : source.pmd.boneFrames) {
                const auto g = unsigned(entry.group) + 1 + hasFacial;
                if (entry.bone >= model.bones.size() || g >= groups.size())
                    return Fail(reason, "invalid-display-bone");
                groups[g].entries.push_back({false, entry.bone});
            }
        } else {
            for (std::size_t i = 2; i < model.frames.size(); ++i) {
                const auto& frame = model.frames[i];
                ModelDisplayGroup group;
                group.names = {decode(frame.names[0]), decode(frame.names[1])};
                for (const auto& entry : frame.entries)
                    if (!entry.type)
                        group.entries.push_back({false, unsigned(entry.index)});
                groups.push_back(std::move(group));
            }
        }
        if (groups.size() > 255)
            return Fail(reason, "display-group-limit");
    }
    if (source.kind == format::ModelKind::Pmd)
        resource->pmdToonNames_ = source.pmd.toonNames;
    resource->document_ = model;
    for (auto& vertex : resource->document_.vertices)
        if (vertex.weightType == 3) {
            if (!Finite(vertex.center) || !Finite(vertex.r0) || !Finite(vertex.r1))
                return Fail(reason, "nonfinite-sdef");
            for (int c = 0; c < 3; ++c) {
                const float middle =
                    vertex.weight[0] * vertex.r0[c] + (1.f - vertex.weight[0]) * vertex.r1[c];
                vertex.r0[c] -= middle;
                vertex.r1[c] -= middle;
            }
        }
    output = std::move(resource);
    reason.clear();
    return true;
}

bool MotionClip::Create(std::shared_ptr<const ModelResource> resource,
                        const format::VmdDocument& source,
                        std::shared_ptr<const MotionClip>& output, std::string& reason) {
    if (!resource)
        return Fail(reason, "missing-model-resource");
    if (!source.cameras.empty() || !source.lights.empty() || !source.shadows.empty())
        return Fail(reason, "unsupported-global-motion");
    auto clip = std::shared_ptr<MotionClip>(new MotionClip);
    clip->resource_ = std::move(resource);
    std::map<std::string, std::size_t> bones, morphs;
    for (std::size_t n = 0; n < clip->resource_->BoneNames().size(); ++n)
        bones.emplace(clip->resource_->BoneNames()[n], n);
    for (std::size_t n = 0; n < clip->resource_->MorphNames().size(); ++n)
        morphs.emplace(clip->resource_->MorphNames()[n], n);
    clip->bones_.resize(clip->resource_->BoneNames().size());
    clip->morphs_.resize(clip->resource_->MorphNames().size());
    for (auto& keys : clip->bones_) {
        format::PmmBoneKey key;
        key.rotation[3] = 1;
        keys.push_back(key);
    }
    for (auto& keys : clip->morphs_)
        keys.push_back({});
    for (const auto& sourceKey : source.bones) {
        std::string name;
        if (!MotionName(sourceKey.name, name))
            return Fail(reason, "invalid-vmd-name-encoding");
        if (sourceKey.frame > std::numeric_limits<std::int32_t>::max() ||
            !Finite(sourceKey.position) || !Finite(sourceKey.rotation))
            return Fail(reason, "invalid-bone-key");
        float squared = 0;
        for (float v : sourceKey.rotation)
            squared += v * v;
        if (std::abs(squared - 1) > 1e-3f)
            return Fail(reason, "unsupported-nonunit-rotation");
        const auto found = bones.find(name);
        if (found == bones.end()) {
            clip->warnings_.push_back("unbound-bone-track:" + name);
            continue;
        }
        format::PmmBoneKey key;
        key.frame = sourceKey.frame;
        key.physicsDisabled =
            sourceKey.interpolation[2] == 0x63 && sourceKey.interpolation[3] == 0x0f;
        std::copy_n(sourceKey.position, 3, key.position.begin());
        std::copy_n(sourceKey.rotation, 4, key.rotation.begin());
        const auto& bone = clip->resource_->Document().bones[found->second].fields;
        ConstrainRotation(key.rotation.data(), bone);
        if (!Finite(key.rotation))
            return Fail(reason, "invalid-constrained-rotation");
        key.interpolation = format::DecodeVmdBoneInterpolation(sourceKey.interpolation);
        for (auto v : key.interpolation)
            if (v > 127)
                return Fail(reason, "invalid-bone-curve");
        clip->bones_[found->second].push_back(key);
    }
    for (const auto& key : source.morphs) {
        std::string name;
        if (!MotionName(key.name, name))
            return Fail(reason, "invalid-vmd-name-encoding");
        if (key.frame > std::numeric_limits<std::int32_t>::max() || !std::isfinite(key.value))
            return Fail(reason, "invalid-morph-key");
        const auto found = morphs.find(name);
        if (found == morphs.end()) {
            clip->warnings_.push_back("unbound-morph-track:" + name);
            continue;
        }
        clip->morphs_[found->second].push_back(key);
    }
    format::PmmDisplayKey display;
    display.visible = 1;
    display.ikEnabled.resize(clip->resource_->chains_.size(), 1);
    clip->displays_.push_back(display);
    for (const auto& key : source.ik) {
        if (key.frame > unsigned(std::numeric_limits<int>::max()))
            return Fail(reason, "invalid-display-frame");
        auto next = display;
        next.frame = key.frame;
        next.visible = key.visible;
        for (const auto& entry : key.entries) {
            std::string name;
            if (!MotionName(entry.name, name))
                return Fail(reason, "invalid-vmd-name-encoding");
            const auto found = bones.find(name);
            std::size_t chain = 0;
            for (; chain < clip->resource_->chains_.size(); ++chain)
                if (found != bones.end() &&
                    clip->resource_->chains_[chain].boneIndex == int(found->second))
                    break;
            if (chain == clip->resource_->chains_.size())
                clip->warnings_.push_back("unbound-ik-track:" + name);
            else
                next.ikEnabled[chain] = entry.enabled;
        }
        clip->displays_.push_back(std::move(next));
    }
    SortTrack(clip->displays_);
    if (clip->displays_.size() > 1000)
        return Fail(reason, "display-key-capacity-exceeded");
    for (auto& keys : clip->bones_)
        SortTrack(keys);
    for (auto& keys : clip->morphs_)
        SortTrack(keys);
    output = std::move(clip);
    reason.clear();
    return true;
}

bool MotionClip::Create(std::shared_ptr<const ModelResource> resource,
                        const format::VpdDocument& source,
                        std::shared_ptr<const MotionClip>& output, std::string& reason) {
    std::shared_ptr<const MotionClip> empty;
    if (!Create(resource, format::VmdDocument{}, empty, reason))
        return false;
    auto clip = std::shared_ptr<MotionClip>(new MotionClip(*empty));
    for (const auto& sourceBone : source.bones) {
        std::string name;
        if (!text::DecodeCp932(sourceBone.name, name) || name.empty())
            return Fail(reason, "invalid-vpd-name-encoding");
        if (!Finite(sourceBone.translation) || !Finite(sourceBone.rotation))
            return Fail(reason, "nonfinite-vpd-pose");
        float squared = 0;
        for (float q : sourceBone.rotation)
            squared += q * q;
        if (std::abs(squared - 1) > 1e-3f)
            return Fail(reason, "unsupported-nonunit-rotation");
        const auto found =
            std::find(resource->BoneNames().begin(), resource->BoneNames().end(), name);
        if (found == resource->BoneNames().end()) {
            clip->warnings_.push_back("unbound-vpd-bone:" + name);
            continue;
        }
        const auto index = std::size_t(found - resource->BoneNames().begin());
        auto& key = clip->bones_[index][0];
        std::copy_n(sourceBone.translation, 3, key.position.begin());
        std::copy_n(sourceBone.rotation, 4, key.rotation.begin());
        ConstrainRotation(key.rotation.data(), resource->Document().bones[index].fields);
        if (!Finite(key.rotation))
            return Fail(reason, "invalid-constrained-rotation");
    }
    output = std::move(clip);
    reason.clear();
    return true;
}

bool ModelInstance::BindMotion(std::shared_ptr<const MotionClip> motion, std::string& reason) {
    if (motion && motion->resource_ != resource_)
        return Fail(reason, "motion-resource-mismatch");
    motion_ = std::move(motion);
    snapshot_ = {};
    reason.clear();
    return true;
}
bool ModelInstance::Prepare(std::uint32_t frame, ModelEvaluation& output,
                            std::string& reason) const {
    if (!resource_)
        return Fail(reason, "missing-model-resource");
    if (frame > std::numeric_limits<std::int32_t>::max())
        return Fail(reason, "invalid-frame");
    std::vector<format::PmmEvaluatedBone> bones(resource_->BoneNames().size());
    std::vector<float> morphs(resource_->MorphNames().size());
    for (auto& bone : bones)
        bone.rotation[3] = 1;
    if (motion_) {
        for (std::size_t n = 0; n < bones.size(); ++n) {
            const auto [left, right] = Bracket(motion_->bones_[n], frame);
            if (left == right) {
                bones[n].translation = left->position;
                bones[n].rotation = left->rotation;
                bones[n].physicsDisabled = left->physicsDisabled;
            } else if (!format::InterpolatePmmBoneSegment(*left, *right, frame, bones[n]))
                return Fail(reason, "bone-evaluation-failed", n);
        }
        for (std::size_t n = 0; n < morphs.size(); ++n) {
            const auto [left, right] = Bracket(motion_->morphs_[n], frame);
            morphs[n] = left == right
                            ? left->value
                            : format::InterpolatePmmMorphValue(left->value, right->value,
                                                               left->frame, right->frame, frame);
        }
    }
    ModelEvaluation next;
    if (!PreparePose(frame, bones, morphs, next, reason))
        return false;
    if (motion_) {
        auto display = std::upper_bound(motion_->displays_.begin(), motion_->displays_.end(), frame,
                                        [](unsigned f, const auto& key) { return f < key.frame; });
        if (display != motion_->displays_.begin())
            --display;
        if (!next.SetIk(display->ikEnabled, reason))
            return false;
    }
    output = std::move(next);
    return true;
}
bool ModelInstance::PreparePose(std::uint32_t frame,
                                const std::vector<format::PmmEvaluatedBone>& poses,
                                const std::vector<float>& morphs, ModelEvaluation& output,
                                std::string& reason) const {
    ModelEvaluation next;
    if (!PreparePoseReusing(frame, poses, morphs, next, reason))
        return false;
    output = std::move(next);
    return true;
}
bool ModelInstance::PreparePoseReusing(std::uint32_t frame,
                                       const std::vector<format::PmmEvaluatedBone>& poses,
                                       const std::vector<float>& morphs, ModelEvaluation& next,
                                       std::string& reason) const {
    next.prepared_ = next.beforeSolved_ = next.afterSolved_ = next.finished_ = false;
    if (!resource_)
        return Fail(reason, "missing-model-resource");
    if (frame > std::numeric_limits<std::int32_t>::max())
        return Fail(reason, "invalid-frame");
    if (poses.size() != resource_->BoneNames().size() ||
        morphs.size() != resource_->MorphNames().size())
        return Fail(reason, "pose-shape-mismatch");
    next.resource_ = resource_;
    next.snapshot_.frame = frame;
    next.snapshot_.morphWeights = morphs;
    next.snapshot_.bones.resize(poses.size());
    for (std::size_t n = 0; n < poses.size(); ++n) {
        if (!Finite(poses[n].translation) || !Finite(poses[n].rotation))
            return Fail(reason, "nonfinite-bone-pose", n);
        next.snapshot_.bones[n].translation = poses[n].translation;
        next.snapshot_.bones[n].rotation = poses[n].rotation;
    }
    for (float weight : morphs)
        if (!std::isfinite(weight))
            return Fail(reason, "nonfinite-morph-pose");
    if (!next.Initialize(reason))
        return false;
    for (std::size_t n = 0; n < poses.size(); ++n)
        next.bones_[n].physicsDisabled = poses[n].physicsDisabled != 0;
    next.prepared_ = true;
    reason.clear();
    return true;
}
bool ModelInstance::Publish(ModelEvaluation&& state, std::string& reason) {
    if (!resource_ || state.resource_ != resource_ || !state.finished_ ||
        state.snapshot_.vertices.size() != resource_->Document().vertices.size())
        return Fail(reason, "evaluation-resource-mismatch");
    // The displaced snapshot becomes next frame's unpublished storage.
    std::swap(snapshot_, state.snapshot_);
    state.prepared_ = state.finished_ = false;
    reason.clear();
    return true;
}
bool ModelInstance::Seek(std::uint32_t frame, std::string& reason) {
    ModelEvaluation next;
    return Prepare(frame, next, reason) && next.Solve(false, nullptr, 0, reason) &&
           next.Solve(true, nullptr, 0, reason) && next.Finish(reason) &&
           Publish(std::move(next), reason);
}
}
