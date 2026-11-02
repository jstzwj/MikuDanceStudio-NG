#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/render_scene_passes.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/cpu_edge.hpp"
#include "mikudancestudio/rhi_screen_texture.hpp"
#include "mikudancestudio/background_placement.hpp"
#include "background_quad.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <stdexcept>
#include <set>
namespace mikudancestudio::render {
namespace {
using namespace rhi;
using namespace cpu_math;
bool Fail(std::string& reason, const std::string& why) {
    reason = why;
    return false;
}
std::filesystem::path Path(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return std::filesystem::u8path(value);
}
std::filesystem::path TexturePath(const format::PmxDocument& doc, int index) {
    if (index < 0)
        return {};
    if (std::size_t(index) >= doc.textures.size())
        throw std::runtime_error("texture_index_range");
    std::string value;
    const auto& encoded = doc.textures[index];
    if (!text::DecodeUtf16Le(encoded.data(), encoded.size(), value) ||
        value.find('\0') != std::string::npos)
        throw std::runtime_error("texture_path_encoding");
    return Path(value);
}
template <std::size_t N> std::string Cp932(const char (&value)[N]) {
    std::string result;
    const auto end = std::find(value, value + N, '\0');
    if (!text::DecodeCp932(std::string_view(value, std::size_t(end - value)), result))
        throw std::runtime_error("texture_path_encoding");
    return result;
}
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float Dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
ModelInputMath ShadowMath() {
    ModelInputMath math;
    math.multiply = [](Matrix& out, const Matrix& a, const Matrix& b) { out = Multiply(a, b); };
    math.normalize = [](Vec3& v) {
        Vec3 out{};
        Normalize(v, out);
        v = out;
    };
    math.lookAtTarget = [](Matrix& out, const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 z{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]}, x{};
        if (!Normalize(z, z) || !Normalize(Cross(up, z), x)) {
            out = {};
            return;
        }
        const auto y = Cross(z, x);
        out = {x[0], y[0], z[0], 0, x[1],         y[1],         z[1],         0,
               x[2], y[2], z[2], 0, -Dot(x, eye), -Dot(y, eye), -Dot(z, eye), 1};
    };
    return math;
}
bool Camera(const runtime::SceneRuntime& scene, ModelFrameInputs& f, std::string& reason,
            std::optional<std::uint8_t> editorModel = std::nullopt) {
    f.world = f.lightRotation = Identity();
    f.scene.camera.distance = -45;
    f.scene.camera.fov = 45;
    f.scene.camera.parentModel = -1;
    f.scene.lightState = {.6f, .6f, .6f, -.5f, -1, -.5f};
    if (const auto* session = scene.Session()) {
        const auto& doc = session->Document();
        f.scene.frame = session->Pose().frame;
        f.scene.camera = session->Pose().camera;
        f.scene.lightState = session->Pose().lightState;
        auto& c = f.scene.camera;
        if (doc.envelope.header.version == format::PmmVersion::V1 && c.parentModel >= 0)
            return Fail(reason, "v1_camera_parent_requires_explicit_import");
        if (editorModel) {
            const bool cameraMode = *editorModel == 255;
            const bool cameraView =
                cameraMode ||
                (doc.appearance.followCamera &&
                 (scene.Playing() || (c.parentModel >= 0 && *editorModel == c.parentModel)));
            Vec3 focus{};
            Matrix parent = Identity(), attachment = Identity();
            if (c.parentModel >= 0) {
                int index;
                if (!scene.DocumentBoneIndex(c.parentModel, std::max(0, c.parentBone), index,
                                             reason) ||
                    !scene.BoneAttachment(c.parentModel, index, parent, reason))
                    return false;
            }
            f.view.editorNavigation = true;
            f.view.editorEye = {doc.cameraState[3], doc.cameraState[4], c.distance};
            f.view.editorFov = c.fov;
            f.view.editorPerspective = c.perspective;
            if (cameraView) {
                if (c.parentModel < 0)
                    focus = c.position;
                else {
                    std::copy_n(parent.begin() + 12, 3, focus.begin());
                    f.view.editorEye = c.position;
                }
            } else if (doc.timeline.cameraReferenceMode != 0) {
                const auto snapshot = scene.Snapshot(*editorModel);
                const auto resource = scene.Resource(*editorModel);
                if (!snapshot || !resource)
                    return Fail(reason, "editor_camera_model_expired");
                if (doc.timeline.cameraReferenceMode == 1) {
                    const auto& skin = snapshot->bones[resource->CameraCenterBone()].skin;
                    std::copy_n(skin.begin() + 12, 3, focus.begin());
                } else if (doc.timeline.cameraReferenceMode == 2) {
                    for (const auto& model : doc.models)
                        if (model.slot == *editorModel && model.selection.selectedBone >= 0) {
                            int index;
                            if (!scene.DocumentBoneIndex(*editorModel, model.selection.selectedBone,
                                                         index, reason))
                                return false;
                            focus = snapshot->bones[std::size_t(index)].worldPosition;
                        }
                }
            }
            if (doc.appearance.followCamera || cameraMode) {
                if (c.parentModel >= 0 && !doc.appearance.suppressCameraTransform)
                    for (unsigned r = 0; r < 3; ++r)
                        for (unsigned col = 0; col < 3; ++col)
                            attachment[r * 4 + col] = parent[col * 4 + r];
            } else if (std::any_of(doc.appearance.cameraAttachmentBasis.begin(),
                                   doc.appearance.cameraAttachmentBasis.end(),
                                   [](float v) { return v != 0; }))
                attachment = doc.appearance.cameraAttachmentBasis;
            const auto rotation =
                Multiply(Multiply(RotationY(c.rotation[1]), RotationX(c.rotation[0])),
                         RotationZ(c.rotation[2]));
            f.lightRotation = Multiply(attachment, rotation);
            f.world = Multiply(Translation({-focus[0], -focus[1], -focus[2]}), f.lightRotation);
            return true;
        }
        Vec3 focus = c.position;
        Matrix attachment = Identity();
        if (c.parentModel >= 0) {
            int index;
            if (!scene.DocumentBoneIndex(c.parentModel, std::max(0, c.parentBone), index, reason) ||
                !scene.BoneAttachment(c.parentModel, index, attachment, reason))
                return false;
            std::copy_n(attachment.begin() + 12, 3, focus.begin());
            if (!(doc.appearance.followCamera || doc.timeline.cameraReferenceMode) ||
                doc.appearance.suppressCameraTransform)
                attachment = Identity();
            attachment[12] = attachment[13] = attachment[14] = 0;
        }
        if (!doc.appearance.followCamera && !doc.timeline.cameraReferenceMode &&
            std::any_of(doc.appearance.cameraAttachmentBasis.begin(),
                        doc.appearance.cameraAttachmentBasis.end(), [](float v) { return v != 0; }))
            attachment = doc.appearance.cameraAttachmentBasis;
        auto rotation = Multiply(Multiply(RotationY(c.rotation[1]), RotationX(c.rotation[0])),
                                 RotationZ(c.rotation[2]));
        f.lightRotation = Multiply(attachment, rotation);
        f.world = Multiply(Translation({-focus[0], -focus[1], -focus[2]}), f.lightRotation);
        // The shared shader producer uses attached camera position as its eye.
        // Native compatibility uses exactly the same explicit input convention.
    } else if (const auto* snapshot = scene.Snapshot(0))
        f.scene.frame = snapshot->frame;
    return true;
}
struct Material {
    std::filesystem::path base, sphere;
    std::array<std::shared_ptr<const MaterialTextureAsset>, 3> textures;
    std::array<float, 4> toonColor{1, 1, 1, 1};
    bool screen = false;
};
ModelMaterialInputs Inputs(const format::MaterialData& src, bool pmd, const Material& saved) {
    ModelMaterialInputs m;
    std::copy_n(src.diffuse, 4, m.diffuse.begin());
    m.ambient = m.diffuse;
    std::copy_n(src.specular, 3, m.specular.begin());
    std::copy_n(src.ambient, 3, m.emissive.begin());
    m.shininess = src.specularPower;
    m.pmd = pmd;
    m.doubleSided = !pmd && (src.flags & 1);
    m.base = saved.base;
    m.sphere = saved.sphere;
    m.sphereMode = src.sphereMode;
    return m;
}
bool Texture(const std::filesystem::path& directory, std::filesystem::path& reference,
             std::shared_ptr<const MaterialTextureAsset>& output, std::string& reason) {
    if (reference.empty())
        return true;
    MaterialTextureAsset asset;
    if (LoadMaterialTexture(directory, reference, asset) != MaterialResourceStatus::Ready)
        return Fail(reason, "texture_missing_or_unsupported:" + reference.generic_u8string());
    if (!PrepareMaterialMipChain(asset, MipPreparationPolicy::BoxLinear))
        return Fail(reason, "texture_mip_preparation");
    output = std::make_shared<const MaterialTextureAsset>(std::move(asset));
    return true;
}
void InstallShadow(ModelPipeline& p, const BuiltinShadowState& shared,
                   const ModelMaterialInputs& material, const std::array<float, 4>& toon,
                   bool receive) {
    p.shadow = shared;
    p.shadow.worldViewProjection =
        Multiply(Multiply(p.shader.world, p.shader.view), p.shader.projection);
    p.shadow.toonColor = toon;
    p.shadow.receive = receive;
    const auto& light = p.shader.lights[0];
    for (unsigned c = 0; c < 3; ++c) {
        p.shadow.color[c] = material.ambient[c] * light.ambient[c] + material.emissive[c];
        p.shadow.specular[c] = material.specular[c] * light.specular[c];
    }
    p.shadow.color[3] = material.diffuse[3];
    p.shadow.specular[3] = material.shininess == 0 ? .1f : material.shininess;
}
}
bool SceneViewInputs(const runtime::SceneRuntime& scene, const SceneRenderSettings& settings,
                     ModelFrameInputs& frame, std::string& reason) {
    if (!settings.width || !settings.height)
        return Fail(reason, "scene_viewport_invalid");
    ModelFrameInputs result;
    result.view.aspect = float(settings.width) / settings.height;
    result.view.viewport = {0, 0, settings.width, settings.height, 0, 1};
    if (!Camera(scene, result, reason, settings.editorModel))
        return false;
    frame = std::move(result);
    reason.clear();
    return true;
}
bool SceneBoneScreenPoints(const runtime::SceneRuntime& scene, std::uint8_t slot, unsigned width,
                           unsigned height, std::vector<cpu_bone::ScreenPoint>& output,
                           std::string& reason, std::optional<std::uint8_t> editorModel) {
    const auto resource = scene.Resource(slot);
    const auto snapshot = scene.Snapshot(slot);
    if (!scene.Session() || !resource || !snapshot || !width || !height || width > 0x7fffffffu ||
        height > 0x7fffffffu)
        return Fail(reason, "bone_projection_scene_unavailable");
    const auto& models = scene.Session()->Document().models;
    const auto model = std::find_if(models.begin(), models.end(),
                                    [slot](const auto& m) { return m.slot == slot; });
    if (model == models.end())
        return Fail(reason, "bone_projection_model_unavailable");
    ModelFrameInputs frame;
    SceneRenderSettings settings;
    settings.width = width;
    settings.height = height;
    settings.editorModel = editorModel;
    if (!SceneViewInputs(scene, settings, frame, reason))
        return false;
    ModelPipeline pipeline;
    if (!BuildModelInputs(frame, {}, {}, pipeline))
        return Fail(reason, "bone_projection_camera_invalid");
    const auto transform = [](const std::array<float, 4>& point, const Matrix& m) {
        std::array<float, 4> out;
        for (unsigned c = 0; c < 4; ++c)
            out[c] =
                point[0] * m[c] + point[1] * m[4 + c] + point[2] * m[8 + c] + point[3] * m[12 + c];
        return out;
    };
    std::vector<cpu_bone::ScreenPoint> points;
    for (std::size_t i = 0; i < model->boneNames.size(); ++i) {
        int index;
        if (!scene.DocumentBoneIndex(slot, int(i), index, reason))
            return false;
        const auto& position = snapshot->bones[std::size_t(index)].worldPosition;
        auto point = transform({position[0], position[1], position[2], 1}, pipeline.shader.world);
        point = transform(point, pipeline.shader.view);
        point = transform(point, pipeline.shader.projection);
        cpu_bone::ScreenPoint screen;
        screen.clipW = point[3];
        if (point[3] > 0) {
            const double x = point[0] / point[3] * double(width) * .5 + width / 2;
            const double y = height / 2 - point[1] / point[3] * double(height) * .5;
            if (std::isfinite(x) && std::isfinite(y) && x > -2147483648. && x < 2147483647. &&
                y > -2147483648. && y < 2147483647.) {
                screen.x = int(x);
                screen.y = int(y);
            }
        }
        points.push_back(screen);
    }
    output = std::move(points);
    reason.clear();
    return true;
}
struct SceneFrameProducer::Impl {
    struct Model {
        std::shared_ptr<const runtime::ModelResource> resource;
        std::unique_ptr<ModelGeometry> geometry;
        ModelSubmissionBuilder submission;
        std::vector<Material> materials;
        std::vector<unsigned> edgeMaterials;
        explicit Model(std::uint64_t id) : submission(id + 16) {}
    };
    struct Accessory {
        std::shared_ptr<const runtime::AccessoryResource> resource;
        std::unique_ptr<ModelGeometry> geometry;
        ModelSubmissionBuilder submission;
        std::vector<Material> materials;
        explicit Accessory(std::uint64_t id) : submission(id + 16) {}
    };
    std::map<std::uint8_t, std::unique_ptr<Model>> models;
    std::map<std::uint8_t, std::unique_ptr<Accessory>> accessories;
    AtlasAsset helper;
    ScreenTextureHistory history;
    std::shared_ptr<const RgbaImage> picture;
    ModelReplayResource pictureTexture, pictureVertices;
    bool rendered = false;
    bool sphereAdd = false, pendingSphereAdd = false, pendingMaterialRebuild = false;
    bool hasMaterialHistory = false, pending = false;
};
SceneFrameProducer::SceneFrameProducer() = default;
SceneFrameProducer::~SceneFrameProducer() = default;
bool SceneFrameProducer::Load(const runtime::SceneRuntime& scene, const SceneRenderSources& sources,
                              const AssetSource& builtins, std::string& reason) try {
    auto next = std::make_unique<Impl>();
    if (impl_)
        next->sphereAdd = impl_->sphereAdd;
    if (impl_)
        next->rendered = impl_->rendered;
    if (sources.picture) {
        if (nextId_ > UINT64_MAX - 0x100000000ull)
            return Fail(reason, "scene_resource_id_exhausted");
        next->picture = sources.picture;
        next->pictureTexture.key = {nextId_, 1};
        next->pictureVertices.key = {nextId_ + 1, 1};
        nextId_ += 0x100000000ull;
        next->pictureTexture.kind = ReplayResourceKind::Texture;
        RgbaImage image;
        if (!PrepareBackgroundImage(*sources.picture, image))
            return Fail(reason, "background_texture_preparation");
        PreparedMipChain chain;
        if (!PreparePortableMipChain(image, MipPreparationPolicy::BoxLinear, chain))
            return Fail(reason, "background_mip_preparation");
        next->pictureTexture.texture.levels = std::move(chain.levels);
    }
    if (!LoadAtlas(builtins, AssetId::AccessoryHelper, next->helper))
        return Fail(reason, "scene_shadow_helper_asset");
    ToonTextureResolver toons(sources.sharedToons, builtins);
    for (unsigned slot = 0; slot < 256; ++slot)
        if (auto resource = scene.Resource(std::uint8_t(slot))) {
            const auto found = sources.models.find(std::uint8_t(slot));
            if (found == sources.models.end())
                return Fail(reason, "model_source_path_missing");
            if (nextId_ > UINT64_MAX - 0x100000000ull)
                return Fail(reason, "scene_resource_id_exhausted");
            const auto id = nextId_;
            nextId_ += 0x100000000ull;
            auto model = std::make_unique<Impl::Model>(id);
            model->resource = resource;
            const auto& doc = resource->Document();
            const bool pmd = resource->SourceKind() == format::ModelKind::Pmd;
            if (doc.vertices.empty()) {
                next->models.emplace(std::uint8_t(slot), std::move(model));
                continue;
            }
            model->geometry = std::make_unique<ModelGeometry>(id, unsigned(doc.vertices.size()),
                                                              doc.header.additionalUvCount);
            // Original PMX load-time ownership: a shared vertex takes the
            // material with the smallest original edge size (first on ties).
            model->edgeMaterials.resize(doc.vertices.size());
            std::vector<float> edgeSizes(doc.vertices.size(), 999.79999f);
            std::size_t cursor = 0;
            for (std::size_t m = 0; m < doc.materials.size(); ++m)
                for (int i = 0; i < doc.materials[m].fields.faceVertexCount; ++i) {
                    const auto vertex = std::size_t(doc.indices.at(cursor++));
                    const auto size = doc.materials[m].fields.edgeSize;
                    if (size < edgeSizes.at(vertex)) {
                        edgeSizes[vertex] = size;
                        model->edgeMaterials[vertex] = unsigned(m);
                    }
                }
            if (!model->geometry->SetIndices(IndexFormat::UInt32, doc.indices.data(),
                                             doc.indices.size() * sizeof(std::int32_t)))
                return Fail(reason, "model_indices");
            for (const auto& record : doc.materials) {
                const auto& m = record.fields;
                Material saved;
                if (pmd) {
                    auto texture = Cp932(m.pmdTexture);
                    const auto split = texture.find('*');
                    saved.base = Path(texture.substr(0, split));
                    if (split != std::string::npos)
                        saved.sphere = Path(texture.substr(split + 1));
                } else {
                    saved.base = TexturePath(doc, m.textureIndex);
                    saved.sphere = TexturePath(doc, m.sphereIndex);
                }
                const auto directory = found->second.parent_path();
                ToonRequest request;
                request.modelDirectory = directory;
                request.missing = pmd ? MissingToonPolicy::Unbound : MissingToonPolicy::Default;
                if (pmd && m.toonReference != 255) {
                    if (m.toonReference >= 10)
                        return Fail(reason, "pmd_toon_slot");
                    const auto& name = resource->PmdToonNames()[m.toonReference];
                    std::string decoded;
                    if (!text::DecodeCp932(
                            std::string_view(name.data(),
                                             std::find(name.begin(), name.end(), 0) - name.begin()),
                            decoded))
                        return Fail(reason, "pmd_toon_encoding");
                    request.kind = ToonKind::ModelFile;
                    request.path = Path(decoded);
                    for (unsigned n = 1; n <= 10; ++n) {
                        char expected[20];
                        std::snprintf(expected, sizeof expected, "toon%02u.bmp", n);
                        if (decoded == expected) {
                            request.kind = ToonKind::Shared;
                            request.sharedSlot = n;
                            break;
                        }
                    }
                } else if (!pmd && m.sharedToon) {
                    request.kind = ToonKind::Shared;
                    request.sharedSlot = unsigned(m.toonReference) + 1;
                } else if (!pmd && m.toonIndex >= 0) {
                    request.kind = ToonKind::ModelFile;
                    request.path = TexturePath(doc, m.toonIndex);
                }
                MaterialTextureAsset toon;
                if (!toons.Load(request, toon))
                    return Fail(reason, "toon_missing_or_unsupported");
                if (request.kind == ToonKind::Shared && toon.origin == TextureOrigin::Builtin)
                    for (unsigned c = 0; c < 3; ++c)
                        saved.toonColor[c] =
                            DefaultToonShadowColors[3 * (request.sharedSlot - 1) + c];
                else if (request.kind != ToonKind::Default && !toon.fallback) {
                    const auto& image = toon.content.image;
                    const auto at = std::size_t(image.height - 1) * image.width * 4;
                    for (unsigned c = 0; c < 3; ++c)
                        saved.toonColor[c] = image.pixels[at + c] * (1.f / 256);
                }
                saved.textures[0] = std::make_shared<const MaterialTextureAsset>(std::move(toon));
                if (!Texture(directory, saved.base, saved.textures[1], reason) ||
                    !Texture(directory, saved.sphere, saved.textures[2], reason))
                    return false;
                model->materials.push_back(std::move(saved));
            }
            next->models.emplace(std::uint8_t(slot), std::move(model));
        }
    if (const auto* session = scene.Session())
        for (const auto& item : session->Document().accessories) {
            auto resource = scene.Accessory(item.slot);
            const auto source = sources.accessories.find(item.slot);
            if (!resource || source == sources.accessories.end())
                return Fail(reason, "accessory_source_missing");
            const auto id = nextId_;
            nextId_ += 0x100000000ull;
            auto accessory = std::make_unique<Impl::Accessory>(id);
            accessory->resource = resource;
            accessory->geometry =
                std::make_unique<ModelGeometry>(id, unsigned(resource->Vertices().size()));
            if (!accessory->geometry->SetIndices(IndexFormat::UInt32, resource->Indices().data(),
                                                 resource->Indices().size() * 4))
                return Fail(reason, "accessory_indices");
            auto* vertices = static_cast<ModelVertex*>(accessory->geometry->MainData());
            for (std::size_t n = 0; n < resource->Vertices().size(); ++n) {
                const auto& v = resource->Vertices()[n];
                std::copy_n(v.position.data(), 3, vertices[n].position);
                std::copy_n(v.normal.data(), 3, vertices[n].normal);
                std::copy_n(v.uv.data(), 2, vertices[n].uv);
            }
            if (!accessory->geometry->Publish())
                return Fail(reason, "accessory_geometry");
            for (const auto& m : resource->Materials()) {
                Material saved;
                const auto split = m.texture.find('*');
                saved.base = Path(m.texture.substr(0, split));
                if (split != std::string::npos)
                    saved.sphere = Path(m.texture.substr(split + 1));
                auto name = saved.base.filename().u8string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                    return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : char(c);
                });
                saved.screen = name == "screen.bmp";
                if (!saved.screen &&
                    !Texture(source->second.parent_path(), saved.base, saved.textures[1], reason))
                    return false;
                if (!Texture(source->second.parent_path(), saved.sphere, saved.textures[2], reason))
                    return false;
                accessory->materials.push_back(std::move(saved));
            }
            next->accessories.emplace(item.slot, std::move(accessory));
        }
    for (const auto& model : next->models)
        for (const auto& material : model.second->resource->Document().materials)
            if (material.fields.sphereMode == 3 && material.fields.textureIndex >= 0)
                next->hasMaterialHistory = true;
    impl_ = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    return Fail(reason, e.what());
}

bool SceneFrameProducer::Build(const runtime::SceneRuntime& scene,
                               const SceneRenderSettings& settings, RenderFrame& output,
                               std::string& reason) try {
    if (impl_)
        impl_->pending = false;
    if (!impl_ || !settings.width || !settings.height || settings.width > 4096 ||
        settings.height > 4096 || settings.shadow.mode < -1 || settings.shadow.mode > 2 ||
        settings.shadow.mapSize < 16 || settings.shadow.mapSize > 4096)
        return Fail(reason, "scene_render_settings");
    ModelFrameInputs frame;
    if (!SceneViewInputs(scene, settings, frame, reason))
        return false;
    const auto* session = scene.Session();
    const auto* doc = session ? &session->Document() : nullptr;
    for (unsigned slot = 0; slot < 256; ++slot)
        if (scene.Resource(std::uint8_t(slot)) && !impl_->models.count(std::uint8_t(slot)))
            return Fail(reason, "model_resource_changed_reload_required");
    if (doc && doc->avi.enabled)
        return Fail(reason, "scene_background_requires_media_service");
    const int requestedMode = settings.shadow.mode < 0
                                  ? (session ? session->Pose().selfShadowMode : 0)
                                  : settings.shadow.mode;
    const int mode = doc && !doc->selfShadowEnabled ? 0 : requestedMode;
    const auto distance = settings.shadow.distance > 0 ? settings.shadow.distance
                          : session                    ? session->Pose().selfShadowDistance
                                                       : .01125f;
    BuiltinShadowState shadow;
    shadow.mode = mode ? BuiltinShadowMode::Material : BuiltinShadowMode::None;
    shadow.rotation = frame.lightRotation;
    shadow.perspective = mode == 2;
    ModelPipeline camera;
    ModelMaterialInputs neutral;
    if (!BuildModelInputs(frame, neutral, {}, camera))
        return Fail(reason, "scene_camera");
    shadow.worldViewProjection =
        Multiply(Multiply(camera.shader.world, camera.shader.view), camera.shader.projection);
    const Vec3 eye = frame.view.editorNavigation ? frame.view.editorEye
                     : frame.scene.camera.parentModel >= 0
                         ? frame.scene.camera.position
                         : Vec3{0, 0, frame.scene.camera.distance};
    Matrix inverse;
    if (!Inverse(frame.world, inverse))
        return Fail(reason, "scene_camera_inverse");
    auto target = TransformPoint(eye, inverse);
    std::copy_n(target.data(), 3, shadow.eye.begin());
    shadow.eye[3] = 1;
    Vec3 light{frame.scene.lightState[3], frame.scene.lightState[4], frame.scene.lightState[5]};
    if (!Normalize(light, light))
        return Fail(reason, "scene_light_direction");
    std::copy_n(light.data(), 3, shadow.lightDirection.begin());
    shadow.lightDirection[3] = 1;
    if (mode && !BuildSelfShadowTransform(frame.world, eye, camera.shader.lights[0].direction,
                                          distance, mode, shadow.lightViewProjection, ShadowMath()))
        return Fail(reason, "scene_shadow_transform");
    RenderFrame result;
    result.finalColor = 1;
    result.source.frame = frame.scene.frame;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    result.targets = {{1, {settings.width, settings.height, TextureFormat::Rgba8, access}},
                      {2,
                       {settings.width, settings.height, TextureFormat::DepthStencil,
                        unsigned(TextureAccess::DepthStencilAttachment)}}};
    FramePass main;
    main.name = "scene";
    main.color = FrameAttachment{1};
    main.color->clear =
        doc && doc->appearance.blackBackground ? ClearColor{0, 0, 0, 0} : ClearColor{1, 1, 1, 0};
    main.depthStencil = FrameAttachment{2};
    const bool picture = doc && doc->picture.enabled && impl_->picture;
    if (picture) {
        const auto& placement = doc->picture;
        if (!std::isfinite(placement.scale))
            return Fail(reason, "background_placement_not_finite");
        BackgroundView view;
        view.width = int(settings.width);
        view.height = int(settings.height);
        view.scale = BackgroundViewScale(view.width);
        const auto rect =
            PlaceBackground({placement.offsetX, placement.offsetY, int(impl_->picture->width),
                             int(impl_->picture->height), placement.scale},
                            view);
        std::array<TexturedScreenVertex, 6> screen;
        WriteBackgroundQuad(screen.data(), rect.left, rect.top, rect.right, rect.bottom);
        std::array<std::array<float, 8>, 6> vertices{};
        for (unsigned i = 0; i < 6; ++i) {
            vertices[i][0] = screen[i].x * 2.f / settings.width - 1.f;
            vertices[i][1] = 1.f - screen[i].y * 2.f / settings.height;
            vertices[i][6] = screen[i].u;
            vertices[i][7] = screen[i].v;
        }
        SnapshotBytes bytes;
        bytes.resize(sizeof vertices);
        std::memcpy(bytes.data(), vertices.data(), sizeof vertices);
        auto& vb = impl_->pictureVertices;
        if (vb.bytes != bytes) {
            if (vb.key.version == UINT64_MAX)
                return Fail(reason, "background_vertex_version_exhausted");
            ++vb.key.version;
            bytes.Seal();
            vb.bytes = std::move(bytes);
        }
        ModelReplayDraw draw;
        draw.vertices = vb.key;
        draw.textures[0] = impl_->pictureTexture.key;
        draw.indexed = false;
        draw.drawVertexCount = 6;
        auto& p = draw.pipeline;
        p.viewport = frame.view.viewport;
        p.raster.depthTest = p.raster.depthWrite = false;
        p.raster.cull = BatchCull::None;
        p.raster.alphaBlend = impl_->rendered;
        p.separateAlpha = true;
        p.alphaOperation = ModelBlendOp::Max;
        p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView = Identity();
        p.shader.diffuse = p.shader.nativeDiffuse = {1, 1, 1, 1};
        p.shader.alphaTest = true;
        p.shader.alphaTestFixed12 = true;
        p.shader.alphaCompare = DepthComparison::GreaterEqual;
        p.shader.alphaReference = 1;
        p.shader.textureTransforms[0] = Identity();
        auto& stage = p.shader.layout.stages[0];
        stage.role = MaterialTextureRole::Base;
        stage.color = MaterialCombine::Modulate;
        stage.colorArg2 = MaterialColorSource::LitDiffuse;
        p.shader.samplers[0].u = p.shader.samplers[0].v = SamplerAddress::Wrap;
        main.draws.push_back(std::move(draw));
    }
    // Visibility does not unload the native picture. Keep its resource lease
    // while hidden so showing it cannot resurrect a retired version.
    if (impl_->picture) {
        result.resources.push_back(impl_->pictureTexture);
        if (!impl_->pictureVertices.bytes.empty())
            result.resources.push_back(impl_->pictureVertices);
    }
    FramePass cast;
    cast.name = "self_shadow";
    if (mode) {
        result.targets.push_back(
            {3,
             {settings.shadow.mapSize, settings.shadow.mapSize, TextureFormat::R32Float, access}});
        result.targets.push_back(
            {4,
             {settings.shadow.mapSize, settings.shadow.mapSize, TextureFormat::DepthStencil,
              unsigned(TextureAccess::DepthStencilAttachment)}});
        cast.color = FrameAttachment{3};
        cast.color->clear = {1, 1, 1, 1};
        cast.depthStencil = FrameAttachment{4};
        main.sampled = {3};
    }
    HistoryDesc history;
    history.width = settings.width;
    history.height = settings.height;
    const auto append = [&](ModelReplayStream& stream,
                            std::vector<ModelReplayDraw>* destination = nullptr) {
        for (auto& r : stream.resources)
            if (std::none_of(result.resources.begin(), result.resources.end(),
                             [&](const auto& x) { return x.key.id == r.key.id; }))
                result.resources.push_back(r);
        for (auto& e : stream.events)
            if (e.op == ModelReplayOp::Draw) {
                if (e.draw.pipeline.shadow.mode == BuiltinShadowMode::Material)
                    e.draw.shadowTarget = 3;
                (destination ? *destination : main.draws).push_back(e.draw);
            }
    };
    ScenePassSettings passes;
    if (doc) {
        passes.shadowBlend = doc->physics.projectedShadowBlend != 0;
        const auto gray = doc->physics.projectedShadowAmbient;
        passes.shadowMaterial.diffuse[3] = .699999988079071f;
        passes.shadowMaterial.ambient = {gray, gray, gray, gray};
        passes.shadowMaterial.specular[3] = 1;
    }
    if (doc && doc->view.groundGrid) {
        for (const auto family : {SceneDrawFamily::GroundLines, SceneDrawFamily::GroundPlane}) {
            ModelReplayStream ground;
            if (!BuildGroundDraw(frame, family, {}, history, main.color->clear, ground))
                return Fail(reason, "scene_ground_pipeline");
            const std::uint64_t offset = family == SceneDrawFamily::GroundPlane ? 2 : 0;
            for (auto& resource : ground.resources)
                resource.key.id += offset;
            for (auto& event : ground.events) {
                if (event.resource.id)
                    event.resource.id += offset;
                if (event.draw.vertices.id)
                    event.draw.vertices.id += offset;
                if (event.draw.indices.id)
                    event.draw.indices.id += offset;
            }
            append(ground);
        }
    }
    const auto modelStart = main.draws.size();
    std::vector<ModelReplayDraw> projected, outlines;
    std::map<unsigned, std::vector<ModelReplayDraw>> modelCasters, accessoryCasters,
        accessoryProjected;
    std::vector<std::uint8_t> order;
    for (const auto& item : impl_->models)
        if (scene.Resource(item.first))
            order.push_back(item.first);
    if (doc) {
        const auto display = [&](unsigned slot) {
            return std::find_if(doc->models.begin(), doc->models.end(),
                                [&](const auto& m) { return m.slot == slot; })
                ->selection.comboIndex;
        };
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&](auto slot) { return display(slot) == 255; }),
                    order.end());
        std::stable_sort(order.begin(), order.end(),
                         [&](auto a, auto b) { return display(a) < display(b); });
        order.erase(std::unique(order.begin(), order.end(),
                                [&](auto a, auto b) { return display(a) == display(b); }),
                    order.end());
    }
    for (auto slot : order) {
        auto& model = *impl_->models.at(slot);
        if (scene.Resource(slot) != model.resource)
            return Fail(reason, "model_resource_changed_reload_required");
        if (!model.geometry)
            continue;
        if (session) {
            const auto& poses = session->Pose().models;
            const auto it = std::find_if(poses.begin(), poses.end(),
                                         [&](const auto& p) { return p.slot == slot; });
            if (it == poses.end() || !it->visible)
                continue;
        }
        const auto* snapshot = scene.Snapshot(slot);
        if (!snapshot || snapshot->vertices.size() != model.resource->Document().vertices.size())
            return Fail(reason, "model_snapshot_missing");
        auto* vertices = static_cast<std::uint8_t*>(model.geometry->MainData());
        const auto extraUv = model.geometry->AdditionalUvCount();
        const auto vertexStride = 32 + 16 * extraUv;
        const bool pmd = model.resource->SourceKind() == format::ModelKind::Pmd;
        const auto& source = model.resource->Document();
        float scale = 1;
        std::uint32_t pmdDiffuse = 0xff000000u;
        if (doc)
            for (const auto& record : doc->models)
                if (record.slot == slot) {
                    scale = record.edgeScale;
                    pmdDiffuse = record.pmdEdgeDiffuse;
                    break;
                }
        Vec3 center{};
        if (!snapshot->bones.empty()) {
            const auto n = snapshot->bones.size() > 1 ? 1 : 0;
            const auto* position = source.bones[n].fields.position;
            center =
                TransformPoint({position[0], position[1], position[2]}, snapshot->bones[n].skin);
        }
        const auto worldCenter = TransformPoint(center, frame.world);
        const auto dx = worldCenter[0] - eye[0], dy = worldCenter[1] - eye[1],
                   dz = worldCenter[2] - eye[2];
        const auto edgeScale = cpu_edge::DistanceScale(std::sqrt(dx * dx + dy * dy + dz * dz),
                                                       frame.scene.camera.fov, scale);
        for (std::size_t n = 0; n < snapshot->vertices.size(); ++n) {
            const auto& v = snapshot->vertices[n];
            auto* output = vertices + n * vertexStride;
            std::memcpy(output, v.position.data(), 12);
            std::memcpy(output + 12, v.normal.data(), 12);
            std::memcpy(output + 24, v.uv.data(), 8);
            for (unsigned u = 0; u < extraUv; ++u)
                std::memcpy(output + 32 + u * 16, v.additionalUv[u].data(), 16);
            auto& edge = model.geometry->Edges()[n];
            const auto& original = source.vertices[n];
            const auto& material = snapshot->materials.at(model.edgeMaterials[n]).material;
            for (unsigned c = 0; c < 3; ++c)
                edge.position[c] =
                    pmd ? cpu_edge::Component(v.position[c], v.normal[c],
                                              original.edgeScale == 0 ? -.004999999888241291f
                                                                      : edgeScale)
                        : cpu_edge::PmxComponent(v.position[c], v.normal[c], edgeScale,
                                                 original.edgeScale, material.edgeSize);
            edge.diffuse = pmd ? pmdDiffuse : cpu_edge::Color(material.edgeColor, mode != 0);
        }
        if (!model.geometry->Publish())
            return Fail(reason, "model_geometry_publish");
        std::vector<ModelMaterialPacket> packets;
        unsigned first = 0;
        for (std::size_t n = 0; n < snapshot->materials.size(); ++n) {
            const auto& pose = snapshot->materials[n];
            const auto& saved = model.materials.at(n);
            const auto count = pose.material.faceVertexCount;
            if (count < 0)
                return Fail(reason, "material_index_range");
            if (count) {
                ModelMaterialPacket packet;
                packet.material = unsigned(n);
                packet.firstIndex = first;
                packet.indexCount = unsigned(count);
                auto material = Inputs(
                    pose.material, model.resource->SourceKind() == format::ModelKind::Pmd, saved);
                if (!BuildModelInputs(frame, material, {}, packet.pipeline))
                    return Fail(reason, "model_pipeline");
                packet.pipeline.vertexLayout = model.geometry->Snapshot().main.vertexLayout;
                if (mode) {
                    InstallShadow(packet.pipeline, shadow, material, saved.toonColor,
                                  material.pmd || (pose.material.flags & 8));
                    auto& s = packet.pipeline.shadow;
                    s.textureMultiply = pose.textureTint;
                    s.textureAdd = pose.textureAdd;
                    s.sphereMultiply = pose.sphereTint;
                    s.sphereAdd = pose.sphereAdd;
                    for (unsigned c = 0; c < 3; ++c)
                        s.toonColor[c] = s.toonColor[c] * pose.toonTint[c] + pose.toonAdd[c];
                    s.toonColor[3] = pose.toonTint[3] + pose.toonAdd[3];
                }
                for (unsigned stage = 0; stage < 3; ++stage) {
                    const auto role = packet.pipeline.shader.layout.stages[stage].role;
                    if (role == MaterialTextureRole::None)
                        continue;
                    packet.textureResources[stage] =
                        saved
                            .textures[role == MaterialTextureRole::Toon ? 0
                                      : role == MaterialTextureRole::Sphere && !saved.sphere.empty()
                                          ? 2
                                          : 1];
                }
                packets.push_back(std::move(packet));
            }
            first += unsigned(count);
        }
        if (first != model.resource->Document().indices.size())
            return Fail(reason, "incomplete_material_coverage");
        ModelReplayStream stream;
        auto built = model.submission.Build(model.geometry->Snapshot(), history, main.color->clear,
                                            packets, stream);
        if (!built)
            return Fail(reason, built.reason);
        append(stream);
        if (mode)
            for (std::size_t n = main.draws.size() - packets.size(); n < main.draws.size(); ++n) {
                const auto& material =
                    snapshot->materials[packets[n - (main.draws.size() - packets.size())].material]
                        .material;
                const bool pmd = model.resource->SourceKind() == format::ModelKind::Pmd;
                if (material.diffuse[3] == .9800000190734863f ||
                    (!pmd && (!(material.flags & 4) || material.edgeColor[3] <= 0)))
                    continue;
                auto draw = main.draws[n];
                draw.shadowTarget = 0;
                draw.pipeline.shadow.mode = BuiltinShadowMode::Depth;
                draw.pipeline.raster.cull =
                    (material.flags & 1) && !pmd ? BatchCull::None : BatchCull::CounterClockwise;
                draw.pipeline.raster.alphaBlend = false;
                draw.pipeline.stencil = false;
                draw.pipeline.shader.alphaTest = false;
                draw.pipeline.shader.layout = {};
                draw.pipeline.viewport =
                    ModelViewport{0, 0, settings.shadow.mapSize, settings.shadow.mapSize, 0, 1};
                draw.textures = {};
                modelCasters[slot].push_back(std::move(draw));
            }
        unsigned edgeFirst = 0;
        for (const auto& pose : snapshot->materials) {
            const auto& material = pose.material;
            const auto count = unsigned(material.faceVertexCount);
            for (const auto family :
                 {SceneDrawFamily::ProjectedModelShadow, SceneDrawFamily::ModelOutline}) {
                const bool ground = family == SceneDrawFamily::ProjectedModelShadow;
                const bool enabled =
                    ground ? doc && doc->view.groundShadow && frame.scene.lightState[4] < 0 &&
                                 (pmd || ((material.flags & 2) && material.edgeColor[3] > 0))
                           : scale > 0 && (pmd ? material.flags != 0 : (material.flags & 16) != 0);
                if (!count || !enabled)
                    continue;
                ModelReplayStream edgeStream;
                edgeStream.target = history;
                edgeStream.clear = main.color->clear;
                edgeStream.events = {{1, ModelReplayOp::Clear}};
                ModelReplayDraw draw;
                draw.firstIndex = edgeFirst;
                draw.indexCount = count;
                if (!BuildScenePassInputs(frame, family, passes, draw.pipeline))
                    return Fail(reason, "scene_edge_pipeline");
                if (!ground && !pmd) {
                    draw.pipeline.shadow = shadow;
                    draw.pipeline.shadow.mode = BuiltinShadowMode::Outline;
                    std::copy_n(material.edgeColor, 4, draw.pipeline.shadow.color.begin());
                }
                if (!ground && pmd && mode && doc) {
                    draw.pipeline.shadow = shadow;
                    draw.pipeline.shadow.mode = BuiltinShadowMode::Outline;
                    for (unsigned c = 0; c < 3; ++c)
                        draw.pipeline.shadow.color[c] =
                            doc->appearance.outlineColor[c] * (1.f / 256.f);
                    draw.pipeline.shadow.color[3] = 1;
                }
                if (ground) {
                    ModelReplayResource helper;
                    helper.kind = ReplayResourceKind::Texture;
                    helper.key = {0x8000000000000001ull, impl_->helper.revision};
                    helper.texture.levels = {impl_->helper.image};
                    edgeStream.resources.push_back(helper);
                    draw.textures[0] = helper.key;
                }
                const auto added =
                    AppendModelGeometry(edgeStream, model.geometry->Snapshot(), draw, true);
                if (!added)
                    return Fail(reason, added.reason);
                append(edgeStream, ground ? &projected : &outlines);
            }
            edgeFirst += count;
        }
    }
    main.draws.insert(main.draws.begin() + modelStart, projected.begin(), projected.end());
    main.draws.insert(main.draws.end(), outlines.begin(), outlines.end());
    std::vector<const format::PmmSceneAccessory*> accessoryOrder;
    if (doc)
        for (const auto& item : doc->accessories)
            if (item.order != 255)
                accessoryOrder.push_back(&item);
    std::stable_sort(accessoryOrder.begin(), accessoryOrder.end(), [](auto* a, auto* b) {
        return a->order == b->order ? a->slot < b->slot : a->order < b->order;
    });
    accessoryOrder.erase(std::unique(accessoryOrder.begin(), accessoryOrder.end(),
                                     [](auto* a, auto* b) { return a->order == b->order; }),
                         accessoryOrder.end());
    std::size_t beforeModels = 0;
    std::set<std::pair<std::uint64_t, unsigned>> screenDraws;
    for (const auto* item : accessoryOrder) {
        const auto& poses = session->Pose().accessories;
        const auto evaluated = std::find_if(poses.begin(), poses.end(),
                                            [&](const auto& p) { return p.slot == item->slot; });
        if (evaluated == poses.end())
            return Fail(reason, "accessory_pose_missing");
        const auto& pose = *evaluated;
        if (pose.visible && scene.Accessory(pose.slot)) {
            auto found = impl_->accessories.find(pose.slot);
            if (found == impl_->accessories.end() ||
                found->second->resource != scene.Accessory(pose.slot))
                return Fail(reason, "accessory_resource_changed_reload_required");
            auto& accessory = *found->second;
            auto local = frame;
            Matrix placement;
            if (!scene.AccessoryPlacement(pose.slot, placement, reason))
                return false;
            local.world = Multiply(placement, frame.world);
            std::vector<ModelMaterialPacket> packets;
            for (const auto& subset : accessory.resource->Subsets()) {
                const auto& material = accessory.resource->Materials().at(subset.material);
                const auto& saved = accessory.materials.at(subset.material);
                AccessoryPassInputs input;
                input.material.diffuse = material.diffuse;
                input.material.ambient = material.ambient;
                std::copy_n(material.specular.data(), 3, input.material.specular.begin());
                std::copy_n(material.emissive.data(), 3, input.material.emissive.begin());
                input.material.shininess = material.shininess;
                input.opacity = pose.opacity;
                input.primaryTexture = bool(saved.textures[1]);
                input.sphereTexture = bool(saved.textures[2]);
                input.unboundScreen = saved.screen;
                input.afterModelPass = int(item->order) >= doc->physics.accessoryRenderOrder;
                input.additive = item->state.additiveBlend != 0;
                const auto name = saved.base.generic_u8string();
                input.textureType =
                    name.find(".sph") != std::string::npos || name.find(".SPH") != std::string::npos
                        ? 1
                    : name.find(".spa") != std::string::npos ||
                            name.find(".SPA") != std::string::npos
                        ? 2
                    : saved.screen ? 3
                                   : 0;
                if (input.sphereTexture) {
                    auto ext = saved.sphere.extension().u8string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    input.textureType = ext == ".spa" ? 5 : 4;
                }
                ModelMaterialPacket packet;
                packet.material = unsigned(packets.size());
                packet.firstIndex = subset.firstIndex;
                packet.indexCount = subset.indexCount;
                if (!BuildAccessoryPassInputs(local, input, {}, packet.pipeline))
                    return Fail(reason, "accessory_pipeline");
                if (mode && pose.shadowEnabled) {
                    auto& s = packet.pipeline.shadow;
                    s = shadow;
                    s.accessory = true;
                    s.lightViewProjection = Multiply(placement, shadow.lightViewProjection);
                    s.worldViewProjection = Multiply(placement, shadow.worldViewProjection);
                    s.lightingWorld = placement;
                    s.lightingWorld[12] = s.lightingWorld[13] = s.lightingWorld[14] = 0;
                    auto inverseScale = Identity();
                    const auto inv = pose.scale == 0 ? 0 : 1.f / (pose.scale * 10.f);
                    inverseScale[0] = inverseScale[5] = inverseScale[10] = inv;
                    s.rotation =
                        Multiply(Multiply(inverseScale, s.lightingWorld), frame.lightRotation);
                    const auto& light = packet.pipeline.shader.lights[0];
                    for (unsigned c = 0; c < 3; ++c) {
                        s.color[c] =
                            (material.ambient[c] * light.ambient[c] + material.emissive[c]) *
                            doc->physics.projectedShadowAmbient;
                        s.diffuse[c] = material.diffuse[c] * light.diffuse[c];
                        s.specular[c] = material.specular[c] * light.specular[c];
                    }
                    s.color[3] = material.diffuse[3] * pose.opacity;
                    s.diffuse[3] = 1;
                    s.specular[3] = material.shininess == 0 ? .1f : material.shininess;
                }
                packet.textureResources[0] = saved.textures[1];
                packet.textureResources[1] = saved.textures[2];
                packets.push_back(std::move(packet));
            }
            ModelReplayStream stream;
            const auto built = accessory.submission.Build(accessory.geometry->Snapshot(), history,
                                                          main.color->clear, packets, stream);
            if (!built)
                return Fail(reason, built.reason);
            const auto start = main.draws.size();
            append(stream);
            for (std::size_t n = 0; n < packets.size(); ++n) {
                const auto& subset = accessory.resource->Subsets()[n];
                const auto& material = accessory.resource->Materials()[subset.material];
                if (mode && pose.shadowEnabled && !item->state.additiveBlend &&
                    material.diffuse[3] != .9800000190734863f) {
                    auto draw = main.draws[start + n];
                    draw.shadowTarget = 0;
                    draw.pipeline.shadow.mode = BuiltinShadowMode::Depth;
                    draw.pipeline.raster.alphaBlend = false;
                    draw.pipeline.stencil = false;
                    draw.pipeline.shader.alphaTest = false;
                    draw.pipeline.shader.layout = {};
                    draw.pipeline.viewport =
                        ModelViewport{0, 0, settings.shadow.mapSize, settings.shadow.mapSize, 0, 1};
                    draw.textures = {};
                    accessoryCasters[pose.slot].push_back(std::move(draw));
                }
                if (pose.shadowEnabled && doc->view.groundShadow && frame.scene.lightState[4] < 0) {
                    auto draw = main.draws[start + n];
                    draw.shadowTarget = 0;
                    if (!BuildScenePassInputs(frame, SceneDrawFamily::ProjectedModelShadow, passes,
                                              draw.pipeline))
                        return Fail(reason, "accessory_projected_pipeline");
                    draw.pipeline.vertexLayout = VertexLayout::PositionNormalUv;
                    draw.pipeline.shader.world = Multiply(placement, draw.pipeline.shader.world);
                    draw.pipeline.shader.layout = {};
                    draw.pipeline.shader.normalizeNormals = true;
                    draw.textures = {};
                    accessoryProjected[pose.slot].push_back(std::move(draw));
                }
            }
            for (std::size_t n = 0; n < accessory.resource->Subsets().size(); ++n)
                if (accessory.materials.at(accessory.resource->Subsets()[n].material).screen) {
                    const auto& draw = main.draws.at(start + n);
                    screenDraws.emplace(draw.vertices.id, draw.firstIndex);
                }
            if (int(item->order) < doc->physics.accessoryRenderOrder) {
                const auto count = main.draws.size() - start;
                std::rotate(main.draws.begin() + modelStart + beforeModels,
                            main.draws.begin() + start, main.draws.end());
                beforeModels += count;
            }
        }
    }
    // Native shadow passes walk accessory slots first, then model slots,
    // independently of the material display order.
    for (const auto* batches : {&accessoryCasters, &modelCasters})
        for (const auto& batch : *batches)
            cast.draws.insert(cast.draws.end(), batch.second.begin(), batch.second.end());
    std::vector<ModelReplayDraw> groundAccessories;
    for (const auto& batch : accessoryProjected)
        groundAccessories.insert(groundAccessories.end(), batch.second.begin(), batch.second.end());
    main.draws.insert(main.draws.begin() + modelStart + beforeModels, groundAccessories.begin(),
                      groundAccessories.end());
    // The stock FX9 effect retains spadd. UV1 with a base texture reads it
    // without assigning it; ordinary sphere materials and accessories assign it.
    // Resolve in final display order, after accessories have been interleaved.
    bool sphereAdd = impl_->sphereAdd, assigned = false, inherited = false;
    for (auto& draw : main.draws) {
        auto& p = draw.pipeline;
        if (p.shadow.mode != BuiltinShadowMode::Material)
            continue;
        bool base = false;
        for (const auto& stage : p.shader.layout.stages)
            base |= stage.role == MaterialTextureRole::Base;
        for (auto& stage : p.shader.layout.stages) {
            if (stage.role != MaterialTextureRole::Sphere)
                continue;
            if (stage.coordinates == MaterialCoordinates::MeshUv1) {
                if (base) {
                    inherited |= !assigned;
                    stage.color = sphereAdd ? MaterialCombine::Add : MaterialCombine::Modulate;
                }
            } else {
                sphereAdd = stage.color == MaterialCombine::Add;
                assigned = true;
            }
        }
    }
    if (mode)
        result.passes.push_back(std::move(cast));
    std::vector<ScreenTextureConsumer> consumers;
    for (std::size_t n = 0; n < main.draws.size(); ++n)
        if (main.draws[n].pipeline.raster.cull == BatchCull::CounterClockwise &&
            screenDraws.count({main.draws[n].vertices.id, main.draws[n].firstIndex}))
            consumers.push_back({result.passes.size(), n});
    result.passes.push_back(std::move(main));
    result.finalColor = 1;
    if (settings.postProcessCopy &&
        !AppendPostProcess(result, 1, 5, settings.width, settings.height)) {
        const auto check = ValidateRenderFrame(result);
        return Fail(reason, check ? "scene_post_process" : check.reason);
    }
    if (doc && (!consumers.empty() || doc->view.captureMode) &&
        !impl_->history.Apply(result, consumers, ScreenCaptureKind(doc->view.captureMode),
                              result.finalColor))
        return Fail(reason, "scene_screen_history");
    if (!ApplyMaterialSampling(result, MaterialSamplingPolicy::PortableTrilinear))
        return Fail(reason, "scene_sampling_policy");
    const auto valid = ValidateRenderFrame(result);
    if (!valid)
        return Fail(reason, valid.reason);
    output = std::move(result);
    impl_->pendingSphereAdd = sphereAdd;
    impl_->pendingMaterialRebuild =
        (inherited && sphereAdd != impl_->sphereAdd) || (picture && !impl_->rendered);
    impl_->pending = true;
    reason.clear();
    return true;
} catch (const std::exception& e) {
    return Fail(reason, e.what());
}
bool SceneFrameProducer::Commit() {
    if (!impl_ || !impl_->pending)
        return false;
    impl_->history.Commit();
    impl_->sphereAdd = impl_->pendingSphereAdd;
    impl_->rendered = true;
    impl_->pending = false;
    return impl_->pendingMaterialRebuild;
}
bool SceneFrameProducer::HasMaterialHistory() const {
    return impl_ && (impl_->hasMaterialHistory || impl_->picture);
}
void SceneFrameProducer::ResetHistory() {
    if (impl_) {
        impl_->history.Reset();
        impl_->sphereAdd = false;
        impl_->rendered = false;
        impl_->pending = false;
    }
}
}
