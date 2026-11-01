#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_editor_overlay.hpp"
#include <array>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <stdexcept>
static void Check(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
struct NativeLine {
    float x, y, z, rhw;
    std::uint32_t argb;
};
struct NativeSprite : NativeLine {
    float u, v;
};
static_assert(sizeof(NativeLine) == 20 && sizeof(NativeSprite) == 28);
struct NativeBuffer {
    std::array<void*, 13> methods{};
    void** vtable = methods.data();
    struct Object {
        void** vtable;
        NativeBuffer* owner;
    } object{methods.data(), this};
    std::array<NativeLine, 10000> vertices{};
    static HRESULT Lock(Object* self, UINT offset, UINT, void** data, DWORD) {
        *data = reinterpret_cast<unsigned char*>(self->owner->vertices.data()) + offset;
        return S_OK;
    }
    static HRESULT Unlock(Object*) { return S_OK; }
    NativeBuffer() {
        methods[11] = reinterpret_cast<void*>(&Lock);
        methods[12] = reinterpret_cast<void*>(&Unlock);
    }
};
struct Result {
    int x, y;
    float w;
    unsigned sprites, lines;
};
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 4)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    using Function = Result* (*)(mdl::ModelRecord*, Result*, void*, const float*, const float*, int,
                                 int, const RECT*, NativeSprite*, unsigned);
    const auto reference = original.Function<Function>(0xC4980);
    rhi::FileAssetSource assets(std::filesystem::absolute(argv[3]));
    unsigned cases = 0;
    for (const char* name : {"pose.pmd", "triangle.pmx", "articulated.pmd", "overlay-offset.pmx",
                             "overlay-types.pmd"}) {
        editor::EditorSession session;
        std::string reason;
        Check(session.ImportModel(std::filesystem::absolute(argv[2]) / name, reason), reason);
        Check(session.SelectBones({0}, 0, reason), reason);
        const auto resource = session.Runtime().Resource(0);
        const auto* snapshot = session.Runtime().Snapshot(0);
        auto model = std::make_unique<mdl::ModelRecord>();
        std::vector<mdl::BoneRecord> bones(resource->EditingBones().size());
        std::vector<unsigned char> selected(bones.size(), 0), edited(bones.size(), 0);
        selected[0] = 1;
        std::vector<mdl::IkChain> chains;
        for (const auto& chain : resource->EditingIkChains()) {
            mdl::IkChain ik{};
            ik.boneIndex = chain.boneIndex;
            ik.targetBone = chain.targetBone;
            ik.enabled = 1;
            ik.linkCount = static_cast<unsigned char>(chain.storage.size());
            ik.links = const_cast<std::uint16_t*>(chain.storage.data());
            chains.push_back(ik);
        }
        model->boneCount = unsigned(bones.size());
        model->boneTable = bones.data();
        model->boneSelection = selected.data();
        model->bonePhysicsState = edited.data();
        model->selectedBone = 0;
        model->physicsMode = resource->SourceKind() == format::ModelKind::Pmx ? 2 : 1;
        model->ikChains = chains.data();
        model->ikChainCount = unsigned(chains.size());
        for (std::size_t i = 0; i < bones.size(); ++i) {
            auto& bone = bones[i];
            const auto& info = resource->EditingBones()[i];
            const auto& rest = resource->Document().bones[i].fields.position;
            std::copy_n(rest, 3, bone.position);
            std::copy_n(snapshot->bones[i].skin.begin(), 16, bone.matInit);
            std::copy_n(info.tailPosition.begin(), 3, bone.tailOffset);
            bone.tailBone = info.tailBone;
            bone.type = info.type;
            bone.flags = info.flags;
            bone.hasRigidBody = info.hasRigidBody;
        }
        NativeBuffer buffer;
        std::vector<unsigned char> renderer(0x3A9E0, 0);
        auto* bufferObject = &buffer.object;
        std::memcpy(renderer.data() + 0x3A9D8, &bufferObject, sizeof bufferObject);
        render::SceneEditorOverlay overlay;
        Check(overlay.Load(assets, reason), reason);
        render::SceneRenderSources sources;
        sources.models = session.Assets().modelPaths;
        render::SceneFrameProducer producer;
        Check(producer.Load(session.Runtime(), sources, assets, reason), reason);
        for (unsigned variant = 0; variant < 6; ++variant) {
            Check(session.Edit(
                      "overlay-state",
                      [&](auto& doc, auto&) {
                          doc.physics.playbackMode = variant % 3;
                          for (std::size_t i = 0; i < doc.models[0].bonePose.size(); ++i) {
                              doc.models[0].bonePose[i].flags[0] = variant == 5;
                              doc.models[0].bonePose[i].flags[1] = variant >= 3 && i % 2 == 0;
                          }
                          return true;
                      },
                      reason),
                  reason);
            std::vector<std::uint32_t> selection;
            const auto active = int(variant % bones.size());
            for (std::size_t i = 0; i < bones.size(); ++i) {
                selected[i] = variant < 3 ? int(i) == active : i % 2 == 0;
                edited[i] = variant >= 3 && i % 2 == 0;
                bones[i].physicsDisabled = variant == 5;
                std::copy_n(session.Runtime().Snapshot(0)->bones[i].skin.begin(), 16,
                            bones[i].matInit);
                if (selected[i])
                    selection.push_back(unsigned(i));
            }
            model->selectedBone = active;
            Check(session.SelectBones(selection, active, reason), reason);
            for (unsigned angle = 0; angle < 4; ++angle) {
                auto camera = session.Runtime().Session()->Pose().camera;
                camera.rotation = angle == 0
                                      ? cpu_math::Vec3{}
                                      : cpu_math::Vec3{.13f * angle, -.37f * angle, .21f * angle};
                camera.distance = -45.25f;
                Check(session.SetCamera(camera, reason), reason);
                for (unsigned width : {511u, 640u, 1280u})
                    for (int mode : {0, 1, 2, 3, 4}) {
                        session.SetEditMode(mode);
                        render::SceneRenderSettings settings;
                        settings.width = width;
                        settings.height = width * 3 / 4;
                        settings.editorModel = 0;
                        settings.shadow.mode = 0;
                        render::ModelFrameInputs inputs;
                        Check(render::SceneViewInputs(session.Runtime(), settings, inputs, reason),
                              reason);
                        rhi::ModelPipeline pipeline;
                        Check(render::BuildModelInputs(inputs, {}, {}, pipeline),
                              "camera pipeline");
                        const auto worldView =
                            cpu_math::Multiply(pipeline.shader.world, pipeline.shader.view);
                        const float scale = float(double(width) * 1.2 / 1280.0);
                        std::memcpy(renderer.data() + 0x3A9D4, &scale, sizeof scale);
                        std::vector<NativeSprite> sprites(10000);
                        RECT rect{0, 0, LONG(width), LONG(settings.height)};
                        Result expected{};
                        Check(reference(model.get(), &expected, renderer.data(),
                                        pipeline.shader.projection.data(), worldView.data(), mode,
                                        session.Document().physics.playbackMode, &rect,
                                        sprites.data(), 0) == &expected,
                              "native return");
                        rhi::RenderFrame frame;
                        Check(producer.Build(session.Runtime(), settings, frame, reason), reason);
                        Check(overlay.Append(session.Runtime(), settings, selection, false, frame,
                                             reason),
                              reason);
                        unsigned actualLines = 0, actualSprites = 0;
                        if (frame.passes.back().name == "editor_bone_overlay")
                            for (const auto& draw : frame.passes.back().draws) {
                                const auto found =
                                    std::find_if(frame.resources.begin(), frame.resources.end(),
                                                 [&](const auto& value) {
                                                     return value.key.id == draw.vertices.id;
                                                 });
                                Check(found != frame.resources.end(), "overlay vertices missing");
                                const auto x = [&](float value) {
                                    return value * 2.f / width - 1.f;
                                };
                                const auto y = [&](float value) {
                                    return 1.f - value * 2.f / settings.height;
                                };
                                if (draw.pipeline.topology == rhi::PrimitiveTopology::LineList) {
                                    actualLines = draw.drawVertexCount / 2;
                                    for (unsigned i = 0; i < draw.drawVertexCount; ++i) {
                                        float actual[3];
                                        std::uint32_t color;
                                        std::memcpy(actual, found->bytes.data() + i * 16, 12);
                                        std::memcpy(&color, found->bytes.data() + i * 16 + 12, 4);
                                        const auto& v = buffer.vertices[i];
                                        const auto rgba = (v.argb & 0xff00ff00u) |
                                                          ((v.argb & 255) << 16) |
                                                          ((v.argb >> 16) & 255);
                                        if (actual[0] != x(v.x) || actual[1] != y(v.y) ||
                                            actual[2] != v.z || color != rgba) {
                                            std::fprintf(
                                                stderr,
                                                "%s width=%u mode=%d line=%u actual=(%.9g,%.9g,%x) expected=(%.9g,%.9g,%x)\n",
                                                name, width, mode, i, actual[0], actual[1], color,
                                                x(v.x), y(v.y), rgba);
                                            Check(false, "native line vertex differs");
                                        }
                                    }
                                } else {
                                    actualSprites = draw.drawVertexCount / 3;
                                    for (unsigned i = 0; i < draw.drawVertexCount; ++i) {
                                        float actual[8];
                                        std::memcpy(actual, found->bytes.data() + i * 32, 32);
                                        const auto& v = sprites[i];
                                        if (actual[0] != x(v.x) || actual[1] != y(v.y) ||
                                            actual[6] != v.u || actual[7] != v.v) {
                                            std::fprintf(
                                                stderr,
                                                "%s width=%u mode=%d sprite=%u actual=(%.9g,%.9g;%.9g,%.9g) expected=(%.9g,%.9g;%.9g,%.9g)\n",
                                                name, width, mode, i, actual[0], actual[1],
                                                actual[6], actual[7], x(v.x), y(v.y), v.u, v.v);
                                            Check(false, "native sprite vertex differs");
                                        }
                                    }
                                }
                            }
                        Check(actualLines == expected.lines && actualSprites == expected.sprites,
                              "native primitive count differs");
                        ++cases;
                    }
            }
        }
    }
    std::printf(
        "Original bone overlay: %u exact vertex/count cases passed; no window or GPU device\n",
        cases);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
