#include "mikudancestudio/scene_editor_overlay.hpp"
#include "mikudancestudio/bone_overlay.hpp"
#include "mikudancestudio/background_placement.hpp"
#include "mikudancestudio/bone_local_axes.hpp"
#include "mikudancestudio/bone_operation_axis.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mikudancestudio::render {
namespace {
using namespace rhi;
using namespace cpu_math;
struct Bone : runtime::BoneEditingInfo {
    int screenX = 393939, screenY = 393939, tailScreenX = 393939, tailScreenY = 393939;
    bool physicsDisabled = false, selected = false, edited = false;
};
struct LineVertex {
    float x, y, z;
    std::uint32_t rgba;
};
static_assert(sizeof(LineVertex) == 16);
using SpriteVertex = std::array<float, 8>;
bool Fail(std::string& reason, const std::string& text) {
    reason = text;
    return false;
}
std::array<float, 4> Transform(const std::array<float, 4>& p, const Matrix& m) {
    std::array<float, 4> out;
    for (unsigned c = 0; c < 4; ++c)
        out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + p[3] * m[12 + c];
    return out;
}
cpu_bone::ScreenPoint Project(const std::array<float, 4>& rest, const Matrix& skin,
                              const ModelPipeline& p, unsigned width, unsigned height) {
    auto point =
        Transform(Transform(Transform(Transform(rest, skin), p.shader.world), p.shader.view),
                  p.shader.projection);
    cpu_bone::ScreenPoint result;
    if (point[3] > 0) {
        const double x = point[0] / point[3] * double(width) * .5 + width / 2;
        const double y = height / 2 - point[1] / point[3] * double(height) * .5;
        if (std::isfinite(x) && std::isfinite(y) && x > -2147483648. && x < 2147483647. &&
            y > -2147483648. && y < 2147483647.)
            result = {int(x), int(y)};
    }
    return result;
}
template <class T> bool Publish(ModelReplayResource& resource, const std::vector<T>& vertices) {
    if (vertices.empty())
        return true; // Keep the last lease while hidden; never revive a retired version.
    SnapshotBytes bytes;
    bytes.resize(vertices.size() * sizeof(T));
    std::memcpy(bytes.data(), vertices.data(), bytes.size());
    if (resource.bytes != bytes) {
        if (resource.key.version == UINT64_MAX)
            return false;
        ++resource.key.version;
        bytes.Seal();
        resource.bytes = std::move(bytes);
    }
    return true;
}
ModelPipeline Pipeline(unsigned width, unsigned height) {
    ModelPipeline p;
    p.viewport = ModelViewport{0, 0, width, height, 0, 1};
    p.raster.depthTest = p.raster.depthWrite = false;
    p.raster.cull = BatchCull::None;
    p.raster.alphaBlend = true;
    p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView = Identity();
    p.shader.diffuse = p.shader.nativeDiffuse = {1, 1, 1, 1};
    return p;
}
}
bool SceneEditorOverlay::Load(const rhi::AssetSource& source, std::string& reason) {
    if (loaded_) {
        reason.clear();
        return true;
    }
    rhi::AtlasAsset atlas;
    if (!rhi::LoadAtlas(source, rhi::AssetId::HudSprites, atlas))
        return Fail(reason, "editor_overlay_atlas_unavailable");
    std::vector<std::uint8_t> bytes;
    std::shared_ptr<const runtime::AccessoryResource> axis;
    if (!source.Read(rhi::AssetId::AxisMesh, bytes))
        return Fail(reason, "editor_overlay_axis_unavailable");
    if (!runtime::AccessoryResource::ParseOperationMesh(bytes.data(), bytes.size(), axis, reason))
        return Fail(reason, "editor_overlay_axis_unavailable:" + reason);
    // Disjoint from SceneFrameProducer's ascending low resource namespace.
    constexpr std::uint64_t base = 0x7000000000000000ull;
    texture_.key = {base, 1};
    texture_.kind = rhi::ReplayResourceKind::Texture;
    texture_.texture.levels.push_back(std::move(atlas.image));
    lines_.key.id = base + 1;
    lines_.vertexLayout = rhi::VertexLayout::PositionColor;
    sprites_.key.id = base + 2;
    axisVertices_.key.id = base + 3;
    axisIndices_.key.id = base + 4;
    axisIndices_.kind = rhi::ReplayResourceKind::Indices;
    axisIndices_.indexFormat = rhi::IndexFormat::UInt32;
    std::vector<SpriteVertex> vertices;
    for (const auto& v : axis->Vertices())
        vertices.push_back({v.position[0], v.position[1], v.position[2], v.normal[0], v.normal[1],
                            v.normal[2], v.uv[0], v.uv[1]});
    if (!Publish(axisVertices_, vertices) || !Publish(axisIndices_, axis->Indices()))
        return Fail(reason, "editor_overlay_version_exhausted");
    axis_ = std::move(axis);
    loaded_ = true;
    reason.clear();
    return true;
}
bool SceneEditorOverlay::Append(const runtime::SceneRuntime& scene,
                                const SceneRenderSettings& settings,
                                const std::vector<std::uint32_t>& selected, bool playing,
                                rhi::RenderFrame& frame, std::string& reason,
                                const ViewportTools* tools) {
    if (!loaded_ || !settings.editorModel || !scene.Session() || !settings.width ||
        !settings.height || settings.width > INT32_MAX || settings.height > INT32_MAX)
        return Fail(reason, "editor_overlay_scene_unavailable");
    const auto& doc = scene.Session()->Document();
    const auto width = settings.width, height = settings.height;
    const float scale = BackgroundViewScale(int(width));
    std::vector<LineVertex> lines;
    std::vector<SpriteVertex> sprites;
    FramePass axisPass;
    auto toolState = tools ? *tools : ViewportTools{};
    const auto appendLine = [&](float x, float y, float ex, float ey, std::uint32_t argb) {
        // Model vertex colors have an explicit RGBA8 boundary; native lines are ARGB.
        const auto rgba = (argb & 0xff00ff00u) | ((argb & 255) << 16) | ((argb >> 16) & 255);
        lines.push_back({x * 2.f / width - 1.f, 1.f - y * 2.f / height, 0, rgba});
        lines.push_back({ex * 2.f / width - 1.f, 1.f - ey * 2.f / height, 0, rgba});
    };
    const auto appendV = [&](int x, int y, int ex, int ey, float size, std::uint32_t color) {
        const double dx = double(ex) - x, dy = double(ey) - y;
        const float length = float(std::sqrt(dx * dx + dy * dy));
        if (length == 0)
            return false;
        const auto coordinate = [&](double numerator, int origin) {
            return float(numerator / length * 8.0 * size + origin);
        };
        appendLine(coordinate(-dy, x), coordinate(dx, y), float(ex), float(ey), color);
        appendLine(float(ex), float(ey), coordinate(dy, x), coordinate(-dx, y), color);
        return true;
    };
    const auto quad = [&](float left, float top, float right, float bottom, float u0, float v0,
                          float u1, float v1) {
        const auto vertex = [&](float sx, float sy, float u, float v) -> SpriteVertex {
            return {sx * 2.f / width - 1.f, 1.f - sy * 2.f / height, 0, 0, 0, 0, u, v};
        };
        for (const auto& v : {vertex(right, top, u1, v0), vertex(right, bottom, u1, v1),
                              vertex(left, top, u0, v0), vertex(left, top, u0, v0),
                              vertex(right, bottom, u1, v1), vertex(left, bottom, u0, v1)})
            sprites.push_back(v);
    };
    const auto marker = [&](int x, int y, float v0, float v1, bool camera = false) {
        // Original x64 stores the leading edge before adding the full extent.
        const float left = camera ? float(x) - scale * 9.5f : float(x - scale * 9.5);
        const float top = camera ? float(y) - scale * 9.5f : float(y - scale * 9.5);
        const float right = camera ? float(x) + scale * 8.5f : float(left + scale * 18.0);
        const float bottom = camera ? float(y) + scale * 8.5f : float(top + scale * 18.0);
        constexpr float u0 = .46414399f, u1 = .49874699f;
        quad(left, top, right, bottom, u0, v0, u1, v1);
    };
    const auto slot = *settings.editorModel;
    if (!playing && slot != 255) {
        const auto resource = scene.Resource(slot);
        const auto snapshot = scene.Snapshot(slot);
        const auto model = std::find_if(doc.models.begin(), doc.models.end(),
                                        [slot](const auto& m) { return m.slot == slot; });
        if (!resource || !snapshot || model == doc.models.end())
            return Fail(reason, "editor_overlay_model_unavailable");
        ModelFrameInputs inputs;
        ModelPipeline pipeline;
        if (!SceneViewInputs(scene, settings, inputs, reason) ||
            !BuildModelInputs(inputs, {}, {}, pipeline))
            return Fail(reason, "editor_overlay_projection_invalid");
        std::vector<Bone> bones(resource->EditingBones().size());
        for (std::size_t i = 0; i < bones.size(); ++i) {
            auto& bone = bones[i];
            static_cast<runtime::BoneEditingInfo&>(bone) = resource->EditingBones()[i];
            const auto& rest = resource->Document().bones[i].fields.position;
            const auto point = Project({rest[0], rest[1], rest[2], 1}, snapshot->bones[i].skin,
                                       pipeline, width, height);
            bone.screenX = point.x;
            bone.screenY = point.y;
            const auto& tail = bone.tailPosition;
            const auto end = Project({tail[0], tail[1], tail[2], 1}, snapshot->bones[i].skin,
                                     pipeline, width, height);
            bone.tailScreenX = end.x;
            bone.tailScreenY = end.y;
        }
        int active = -1;
        std::vector<bool> selection(model->bonePose.size(), false);
        for (const auto index : selected) {
            if (index >= selection.size())
                return Fail(reason, "editor_overlay_selection_expired");
            selection[index] = true;
        }
        for (std::size_t i = 0; i < model->bonePose.size(); ++i) {
            int index;
            if (!scene.DocumentBoneIndex(slot, int(i), index, reason))
                return false;
            auto& bone = bones[std::size_t(index)];
            bone.physicsDisabled = model->bonePose[i].flags[0] != 0;
            bone.edited = model->bonePose[i].flags[1] != 0;
            bone.selected = selection[i];
            if (model->selection.selectedBone == int(i))
                active = index;
        }
        if (tools && active >= 0 &&
            cpu_bone::OperationAxisActive(bones[std::size_t(active)].type,
                                          bones[std::size_t(active)].flags, toolState.operation,
                                          toolState.dragMode)) {
            struct AxisBone {
                cpu_bone::BoneType type;
                std::uint16_t flags;
                float position[3], matInit[16], localAxes[6];
                int tailBone;
                char name[20]{};
            };
            std::vector<AxisBone> axes(bones.size());
            for (std::size_t i = 0; i < axes.size(); ++i) {
                auto& bone = axes[i];
                const auto& fields = resource->Document().bones[i].fields;
                bone.type = bones[i].type;
                bone.flags = bones[i].flags;
                std::copy_n(fields.position, 3, bone.position);
                std::copy_n(fields.localAxes, 6, bone.localAxes);
                std::copy_n(snapshot->bones[i].skin.data(), 16, bone.matInit);
                bone.tailBone = bones[i].tailBone < 0 ? 0 : bones[i].tailBone;
                std::string name;
                if (text::EncodeCp932(resource->BoneNames()[i], name))
                    std::copy_n(name.data(), std::min(name.size(), sizeof bone.name - 1),
                                bone.name);
            }
            struct AxisModel {
                AxisBone* boneTable;
                int selectedBone, physicsMode;
            } axisModel{axes.data(), active,
                        resource->SourceKind() == format::ModelKind::Pmx ? 2 : 1};
            Matrix axisWorld;
            const auto eye = inputs.view.editorNavigation ? inputs.view.editorEye
                             : inputs.scene.camera.parentModel >= 0
                                 ? inputs.scene.camera.position
                                 : Vec3{0, 0, inputs.scene.camera.distance};
            const auto fov =
                inputs.view.editorNavigation ? inputs.view.editorFov : inputs.scene.camera.fov;
            if (cpu_bone::OperationAxis(
                    axes[std::size_t(active)], toolState.operation, toolState.dragMode,
                    toolState.coordinateSystem, inputs.world, eye, fov,
                    [&](float* basis) {
                        cpu_bone::LocalAxes(&axisModel, basis,
                                            [](float* v) { cpu_model::Math{}.normalize(v, v); });
                    },
                    [](const auto& a, const auto& b) { return Multiply(a, b); }, axisWorld)) {
                for (float value : axisWorld)
                    if (!std::isfinite(value))
                        return Fail(reason, "editor_overlay_axis_transform_invalid");
                constexpr std::uint64_t depth = 0x7000000000000010ull;
                frame.targets.push_back({depth,
                                         {width, height, TextureFormat::DepthStencil,
                                          std::uint32_t(TextureAccess::DepthStencilAttachment)}});
                axisPass.name = "editor_operation_axis";
                axisPass.color = FrameAttachment{frame.finalColor, AttachmentLoad::Load};
                axisPass.depthStencil = FrameAttachment{depth};
                auto local = inputs;
                local.world = axisWorld;
                for (const auto& subset : axis_->Subsets()) {
                    const auto& m = axis_->Materials()[subset.material];
                    ModelMaterialInputs material;
                    material.diffuse = m.diffuse;
                    material.ambient = m.ambient;
                    std::copy_n(m.specular.data(), 3, material.specular.begin());
                    std::copy_n(m.emissive.data(), 3, material.emissive.begin());
                    material.shininess = m.shininess;
                    ModelReplayDraw draw;
                    draw.vertices = axisVertices_.key;
                    draw.indices = axisIndices_.key;
                    draw.firstIndex = subset.firstIndex;
                    draw.indexCount = subset.indexCount;
                    draw.vertexCount = std::uint32_t(axis_->Vertices().size());
                    if (!BuildModelInputs(local, material, {}, draw.pipeline))
                        return Fail(reason, "editor_overlay_axis_pipeline_invalid");
                    draw.pipeline.shader.layout = {};
                    draw.pipeline.shader.normalizeNormals = true;
                    axisPass.draws.push_back(std::move(draw));
                }
            }
        }
        struct Relation {
            int boneIndex, targetBone;
            bool enabled;
            std::uint8_t linkCount;
            const std::uint16_t* links;
        };
        std::vector<Relation> relations;
        std::map<int, bool> ik;
        for (std::size_t i = 0; i < model->ikSources.size(); ++i) {
            int index;
            if (!scene.DocumentBoneIndex(slot, model->ikSources[i], index, reason))
                return false;
            if (i < model->ikPose.size())
                ik[index] = model->ikPose[i] != 0;
        }
        for (const auto& chain : resource->EditingIkChains()) {
            const auto owner = ik.find(chain.boneIndex);
            const bool enabled = owner == ik.end() || owner->second;
            relations.push_back({chain.boneIndex, chain.targetBone, enabled,
                                 std::uint8_t(chain.storage.size()), chain.storage.data()});
        }
        const auto boneAt = [&](int i) -> const auto& { return bones[std::size_t(i)]; };
        toolState.centerX = active >= 0 ? boneAt(active).screenX : 393939;
        toolState.centerY = active >= 0 ? boneAt(active).screenY : 393939;
        const auto selectedAt = [&](int i) { return boneAt(i).selected; };
        const auto editedAt = [&](int i) { return boneAt(i).edited; };
        cpu_bone::OverlayLines(
            int(bones.size()), boneAt, int(relations.size()),
            [&](int i) -> const auto& { return relations[std::size_t(i)]; }, selectedAt, editedAt,
            active, doc.timeline.editMode, doc.physics.playbackMode,
            resource->SourceKind() == format::ModelKind::Pmx, scale, appendV, appendLine);
        cpu_bone::OverlayMarkers(int(bones.size()), boneAt, selectedAt, editedAt, active,
                                 doc.timeline.editMode, doc.physics.playbackMode,
                                 [&](int index, float v0, float v1) {
                                     marker(boneAt(index).screenX, boneAt(index).screenY, v0, v1);
                                 });
    } else if (!playing && scene.Session()->Pose().camera.parentModel < 0) {
        marker(int(width / 2), int(height / 2), .35086f, .385463f, true);
    }
    if (tools) {
        toolState.camera = slot == 255;
        toolState.playing = playing;
        toolState.editMode = doc.timeline.editMode;
        AppendViewportTools(toolState, float(width), float(height), scale, quad);
        if (toolState.selectionBox && !playing)
            AppendSelectionBox(toolState.box[0], toolState.box[1], toolState.box[2],
                               toolState.box[3], appendLine);
    }
    // The replay contract requires a pure clear to have no upload resources.
    // Drop leases only for that case and publish fresh versions on revival.
    if (lines.empty() && sprites.empty() && axisPass.draws.empty() &&
        std::none_of(frame.passes.begin(), frame.passes.end(),
                     [](const auto& p) { return !p.draws.empty(); })) {
        retired_ = true;
        const auto valid = ValidateRenderFrame(frame);
        if (!valid)
            return Fail(reason, valid.reason.c_str());
        reason.clear();
        return true;
    }
    if (retired_) {
        for (auto* resource : {&texture_, &lines_, &sprites_, &axisVertices_, &axisIndices_})
            if (resource->key.version == UINT64_MAX)
                return Fail(reason, "editor_overlay_version_exhausted");
        ++texture_.key.version;
        if (!lines_.bytes.empty())
            ++lines_.key.version;
        if (!sprites_.bytes.empty())
            ++sprites_.key.version;
        ++axisVertices_.key.version;
        ++axisIndices_.key.version;
        retired_ = false;
    }
    if (!Publish(lines_, lines) || !Publish(sprites_, sprites))
        return Fail(reason, "editor_overlay_version_exhausted");
    // A revived upload has a new version; update draws made before retirement handling.
    for (auto& draw : axisPass.draws) {
        draw.vertices = axisVertices_.key;
        draw.indices = axisIndices_.key;
    }
    FramePass pass;
    pass.name = "editor_bone_overlay";
    pass.color = FrameAttachment{frame.finalColor, AttachmentLoad::Load};
    const auto add = [&](const ModelReplayResource& resource, std::uint32_t count, bool textured) {
        ModelReplayDraw draw;
        draw.vertices = resource.key;
        draw.indexed = false;
        draw.drawVertexCount = count;
        draw.pipeline = Pipeline(width, height);
        auto& p = draw.pipeline;
        p.vertexLayout = resource.vertexLayout;
        p.topology = textured ? PrimitiveTopology::TriangleList : PrimitiveTopology::LineList;
        if (textured) {
            draw.textures[0] = texture_.key;
            p.shader.alphaTest = true;
            p.shader.alphaCompare = DepthComparison::GreaterEqual;
            p.shader.alphaReference = 1;
            auto& stage = p.shader.layout.stages[0];
            stage.role = MaterialTextureRole::Base;
            stage.color = MaterialCombine::Modulate;
            stage.colorArg2 = MaterialColorSource::LitDiffuse;
            p.shader.textureTransforms[0] = Identity();
            auto& sampler = p.shader.samplers[0];
            sampler.min = sampler.mag = sampler.mip = SamplerFilter::Linear;
            sampler.u = sampler.v = SamplerAddress::Wrap;
        }
        pass.draws.push_back(std::move(draw));
    };
    if (!lines.empty())
        add(lines_, std::uint32_t(lines.size()), false);
    if (!sprites.empty())
        add(sprites_, std::uint32_t(sprites.size()), true);
    // Leases stay present even when draws disappear, including playback/unload.
    frame.resources.push_back(texture_);
    if (!lines_.bytes.empty())
        frame.resources.push_back(lines_);
    if (!sprites_.bytes.empty())
        frame.resources.push_back(sprites_);
    frame.resources.push_back(axisVertices_);
    frame.resources.push_back(axisIndices_);
    if (!axisPass.draws.empty())
        frame.passes.push_back(std::move(axisPass));
    if (!pass.draws.empty())
        frame.passes.push_back(std::move(pass));
    const auto valid = ValidateRenderFrame(frame);
    if (!valid)
        return Fail(reason, valid.reason.c_str());
    reason.clear();
    return true;
}
}
